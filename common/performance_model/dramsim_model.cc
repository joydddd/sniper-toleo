#include "dramsim_model.h"
#include "simulator.h"
#include "config.h"
#include "config.hpp"
#include <cstdlib>
#include <stdio.h>
#include <stdlib.h>

#if 1
#define MYTRACE_ENABLED
   extern Lock iolock;
#include "core_manager.h"
#define MYTRACE(...)                                    \
{ if (log_trace && status_ == DRAMSIM_RUNNING && sim_status_ == SIM_ROI){                      \
    ScopedLock l(iolock);                               \
    fflush(f_trace);                                    \
    fprintf(f_trace, __VA_ARGS__);                      \
    fprintf(f_trace, "\n");                             \
    fflush(f_trace);                                    \
}}
#define CALLBACKTRACE(...)                              \
{ if (log_trace && status_ == DRAMSIM_RUNNING && sim_status_ == SIM_ROI){         \
    ScopedLock l(iolock);                               \
    fflush(f_callback_trace);                                    \
    fprintf(f_callback_trace, __VA_ARGS__);                      \
    fprintf(f_callback_trace, "\n");                             \
    fflush(f_callback_trace);                                    \
}}
#else
#define MYTRACE(...) {}
#define CALLBACKTRACE(...) {}
#endif

DRAMsimCntlr::DRAMsimCntlr(uint32_t _dram_cntlr_id, uint32_t _ch_id, SubsecondTime default_latency, DramType dram_type_, bool _log_trace):
   dram_type(dram_type_),
   config_prefix(dram_type == DramType::SYSTEM_DRAM ? "perf_model/dram/dramsim/"
                : dram_type == DramType::CXL_MEMORY ? "perf_model/cxl/memory_expander_" + itostr((unsigned int)_dram_cntlr_id) + "/dram/dramsim/" 
                : "perf_model/cxl/vnserver/dram/dramsim/"),
   epoch_size(Sim()->getCfg()->getInt(config_prefix + "epoch")),
   dram_period(ComponentPeriod::fromFreqHz(Sim()->getCfg()->getFloat( config_prefix + "frequency") * 1000000)),// MHz to Hz
   epoch_period(epoch_size * dram_period.getPeriod()),
   ch_id(_ch_id),
   dram_cntlr_id(_dram_cntlr_id),
   log_trace(_log_trace),
   status_(DRAMSIM_NOT_STARTED),
   sim_status_(SIM_NOT_STARTED),
   mem_system_(NULL),
   clk_(0),
   t_start_(SubsecondTime::Zero()),
   t_latest_req_(SubsecondTime::Zero()),
   clk_latest_req_(0),
   epoch_acc_late_launch(0),epoch_total_launched(0),
   f_trace(NULL),
   f_callback_trace(NULL),
   read_lat_generator(config_prefix),
   total_read_lat(0),
   num_of_reads(0)
{
   const char* sniper_root = getenv("SNIPER_ROOT");
   fprintf(stderr, "SNIPER ROOT %s\n", sniper_root);
   std::string config = std::string(sniper_root) + "/DRAMsim3/configs/" + Sim()->getCfg()->getString(config_prefix + "config").c_str() + ".ini";
   std::cerr << "DRAMsim3 config " << config;
   std::string output_dir(Sim()->getCfg()->getString(config_prefix + "output_dir").c_str());
   if (dram_type == DramType::CXL_MEMORY || dram_type == DramType::CXL_VN) 
      output_dir.append("/dramsim_cxl" + std::to_string(dram_cntlr_id) + "_" + std::to_string(ch_id));
   else  output_dir.append("/dramsim_" + std::to_string(dram_cntlr_id) + "_" + std::to_string(ch_id));
   std::system(("mkdir -p " + output_dir).c_str());

   mem_system_ = new dramsim3::MemorySystem(
       config, output_dir,
       std::bind(&DRAMsimCntlr::ReadCallBack, this, std::placeholders::_1),
       std::bind(&DRAMsimCntlr::WriteCallBack, this, std::placeholders::_1));
   read_lat_generator.add_latency(default_latency);

#ifdef MYTRACE_ENABLED
   if (log_trace && (dram_type == DramType::CXL_MEMORY || dram_type == DramType::CXL_VN)){
    f_trace = fopen(("dramsim_cxl_" + std::to_string(dram_cntlr_id) + "_" + std::to_string(ch_id) + ".trace").c_str(), "w+");
    f_callback_trace = fopen(("dramsim_cxl_" + std::to_string(dram_cntlr_id) + "_" + std::to_string(ch_id) + ".callback.trace").c_str(), "w+");
   } else if (log_trace){
      f_trace = fopen(("dramsim_" + std::to_string(dram_cntlr_id) + "_" + std::to_string(ch_id) + ".trace").c_str(), "w+");
      f_callback_trace = fopen(("dramsim_" + std::to_string(dram_cntlr_id) + "_" + std::to_string(ch_id) + ".callback.trace").c_str(), "w+");
   }
#endif
}


DRAMsimCntlr::~DRAMsimCntlr(){
   delete mem_system_;
#ifdef MYTRACE_ENABLED
   if (log_trace)
    fclose(f_trace);
#endif
}

uint64_t DRAMsimCntlr::runDRAMsim(uint64_t target_cycle){ // run DRAMsim until cycle count
   uint64_t reqs_issued = 0;
   // run DRAMsim until 
   auto it = pending_reqs_.begin();
   for (; clk_ < target_cycle; clk_++) {
      if (it != pending_reqs_.end() && it->first <= clk_){ // req cycle <= current clk // use while to handle multiple requests in the same cycle. 
         uint64_t addr = it->second & (~TRANS_IS_WRITE_MASK);
         bool is_write = it->second >> TRANS_IS_WRITE_OFFSET;
         bool get_next = mem_system_->WillAcceptTransaction(addr, is_write);
         if (get_next) {
            // fprintf(stderr, "[DRAMSIM #%d] @0x%016lx send Req %ld [%ld]cycle \n", ch_id, it->second.addr,  it->first, clk_);
            mem_system_->AddTransaction(addr, is_write);
            MYTRACE("0x%lX\t%s\t%lu", addr, is_write ? "WRITE":"READ", clk_);
            in_flight_reqs_.insert(std::make_pair(it->second, clk_));
            it = pending_reqs_.erase(it);
            reqs_issued++;
         }
      }
      mem_system_->ClockTick();
   }
   return reqs_issued;
}

void DRAMsimCntlr::ReadCallBack(uint64_t addr){
   DramTrans trans = addr;
   auto req_range = in_flight_reqs_.equal_range(trans);
   auto req_it = req_range.first;
   uint64_t req_clk = req_it->second;
   in_flight_reqs_.erase(req_it);
   CALLBACKTRACE("0x%lX\tREAD\t%lu\t%lu", addr, req_clk, clk_-req_clk);
   total_read_lat += clk_ - req_clk;
   num_of_reads++;
   read_lat_generator.add_latency((clk_ - req_clk) * dram_period.getPeriod());
}

void DRAMsimCntlr::WriteCallBack(uint64_t addr) {
   DramTrans trans = addr + TRANS_IS_WRITE_MASK;
   auto req_range = in_flight_reqs_.equal_range(trans);
   auto req_it = req_range.first;
   uint64_t req_clk = req_it->second;
   in_flight_reqs_.erase(req_it);
   CALLBACKTRACE("0x%lX\tWRITE\t%lu\t%lu", addr, req_clk, clk_-req_clk);   
}

SubsecondTime DRAMsimCntlr::addTrans(SubsecondTime t_sniper, IntPtr addr, bool is_write){
   if (status_ != DRAMSIM_RUNNING){ // do nothing if DRAMsim isn't running
      return read_lat_generator.get_latency();
   }

   if (t_sniper < t_start_ && sim_status_ == SIM_WARMUP) return read_lat_generator.get_latency(); // skip if request is too early
   LOG_ASSERT_ERROR(t_sniper >= t_start_, "DRAM Request too early: t_sniper %ld, t_start %ld", t_sniper.getNS(), t_start_.getNS());
   
   // Make sure req cycle is after current cycle for Dramsim3. skip transaction if it happens before current dramsim3 cycle. 
   uint64_t req_clk =  (t_sniper - t_start_).getInternalDataForced() / dram_period.getPeriod().getInternalDataForced();
   if (req_clk < clk_ && sim_status_ == SIM_WARMUP) return read_lat_generator.get_latency(); // skip if request is too early
   LOG_ASSERT_ERROR(req_clk >= clk_, "DRAM Request outside of Epoch: req_clk %ld, clk_ %ld", req_clk, clk_);

   // update t_latest_req
   t_latest_req_ = t_sniper > t_latest_req_ ? t_sniper : t_latest_req_;
   clk_latest_req_ = req_clk > clk_latest_req_ ? req_clk : clk_latest_req_;
//    fprintf(stderr, "[DRAMSIM #%d] @0x%016lx Add Req %ld cycle t %ld \n", ch_id, addr, req_clk, t_sniper.getNS());

   // add request to pending_reqs_ Map
   DramTrans trans = addr + (is_write ? TRANS_IS_WRITE_MASK : 0);
   pending_reqs_.insert(std::pair<uint64_t, DramTrans>(req_clk, trans));

   // runDRAMsim(clk_latest_req_ > epoch_size ? clk_latest_req_ - epoch_size : 0);

   return read_lat_generator.get_latency();
}

float DRAMsimCntlr::advance(SubsecondTime t_barrier){
   if (status_ == DRAMSIM_NOT_STARTED || status_ == DRAMSIM_DONE) return 1; // Do nothing
   if (status_ == DRAMSIM_AWAITING){ // set t_start
      LOG_ASSERT_ERROR(clk_ == 0, "clk_ %lu", clk_);
      t_start_ = t_barrier;
      status_ = DRAMSIM_RUNNING;
      fprintf(stderr, "[DRAMSIM #%d] First Barrier at %ld ns, clk %lu\n", ch_id, t_barrier.getNS(), clk_);
      pending_reqs_.clear();
      in_flight_reqs_.clear();
      return 1;
   }
   if (status_ == DRAMSIM_RUNNING){ // run DRAMsim/
      uint64_t barrier_clk =  (t_barrier - t_start_).getInternalDataForced() / dram_period.getPeriod().getInternalDataForced();
      uint64_t mem_reqs_issued;
         mem_reqs_issued = runDRAMsim(barrier_clk > epoch_size ? barrier_clk - epoch_size : 0);

      // calculate last issue requests
      float bp_factor = 1.0f;

      epoch_total_launched += mem_reqs_issued;
      if (clk_ - check_point > 10000 && sim_status_ == SIM_ROI){  // no bp_factor adjust in FASTFORWARD region
         // check if there is any long in-flight requests
         for (auto it = in_flight_reqs_.begin(); it != in_flight_reqs_.end(); it++){
            LOG_ASSERT_ERROR(it->second + 10000 >= check_point, "[DRAMSIM #%d] clk_ %lu Long in-flight request 0x%lX @clk %lu\n", ch_id, clk_, it->first, it->second);
         }


         auto it = pending_reqs_.upper_bound(clk_);
         auto late_issue_reqs = std::distance(pending_reqs_.begin(), it);

         // fprintf(stderr, "[DRAMSIM #%d] Barrier at %ld ns, clk %lu, %lu (%lu) late issue reqs, %lu reqs issued\n", ch_id, t_barrier.getNS(), barrier_clk, late_issue_reqs , epoch_acc_late_launch, epoch_total_launched);
         if (epoch_total_launched > 0){
            bp_factor = ((float)late_issue_reqs - epoch_acc_late_launch) / (float)epoch_total_launched + 1;
         }

         // if (late_issue_reqs > epoch_total_launched * 0.5){
         //    fprintf(stderr, "[DRAMSIM #%d] Barrier at %ld ns, clk %lu, %lu (%lu) late issue reqs, %lu reqs issued\n", ch_id, t_barrier.getNS(), barrier_clk, late_issue_reqs , epoch_acc_late_launch, epoch_total_launched);
         // }

         /* before bp_factor stablize */
         if (late_issue_reqs > epoch_total_launched*3) {
            auto it_too_old = pending_reqs_.upper_bound( clk_ - (clk_ - check_point) * 0.3);
            uint64_t too_old_reqs = std::distance(pending_reqs_.begin(), it_too_old);
            fprintf(stderr, "%d[DRAMSIM #%d] Erase %lu too old requests bp_factor = %f\n", dram_type ,ch_id, too_old_reqs, bp_factor);
            fprintf(stderr,
                    "%d[DRAMSIM #%d] Barrier at %ld ns, clk %lu, %lu (%lu) "
                    "late issue reqs, %lu reqs issued %lu\n",
                    dram_type, ch_id, t_barrier.getNS(), barrier_clk,
                    late_issue_reqs, epoch_acc_late_launch,
                    epoch_total_launched, mem_reqs_issued);
            late_issue_reqs -= too_old_reqs;
            pending_reqs_.erase(pending_reqs_.begin(), it_too_old);
         }

         if(epoch_total_launched > 0) {
            float acc_rate = (float)late_issue_reqs / epoch_total_launched;
            bp_factor = acc_rate < 0.2 ? ( 0.8 + (acc_rate / 0.2) * 0.2) * bp_factor : bp_factor;
            bp_factor = acc_rate > 0.5 ? ( 1.1 - 1.0 / (acc_rate + 0.5) * 0.1) * bp_factor : bp_factor;
         }

         epoch_acc_late_launch = late_issue_reqs;
         epoch_total_launched = 0;
         check_point = clk_;
      } else if (sim_status_ != SIM_ROI) { // if not in ROI, erase all late issue request.
         // check if there is any long in-flight requests
         for (auto it = in_flight_reqs_.begin(); it != in_flight_reqs_.end(); it++){
            LOG_ASSERT_ERROR(it->second >= check_point, "[DRAMSIM #%d] clk_ %lu Long in-flight request 0x%lX @clk %lu\n", ch_id, clk_, it->first, it->second);
         }
          auto it = pending_reqs_.upper_bound(clk_);
          pending_reqs_.erase(pending_reqs_.begin(), it);
          epoch_acc_late_launch = 0;
          epoch_total_launched = 0;
          check_point = clk_;
      }


      return bp_factor;
   }
   return 1;
}

void DRAMsimCntlr::start(InstMode::inst_mode_t sim_status){
   LOG_ASSERT_ERROR(status_ != DRAMSIM_DONE, "DRAMSim3 has already fninshed running");
   if (status_ != DRAMSIM_NOT_STARTED){
      LOG_ASSERT_ERROR(sim_status_ == SIM_WARMUP, "DRAMsim3 did not enter warmup mode");
      sim_status_ = SIM_ROI;
      fprintf(stderr, "[DRAMSIM #%d] Enter ROI %lu\n", ch_id, clk_);
      
      {// erase all late issue requests
         auto it = pending_reqs_.upper_bound(clk_);
         pending_reqs_.erase(pending_reqs_.begin(), it);
         epoch_acc_late_launch = 0;
         epoch_total_launched = 0;
         check_point = clk_;
      }
   } else {
      fprintf(stderr, "[DRAMSIM #%d] Start DRAMsim3\n", ch_id);
      clk_ = 0;
      status_ = DRAMSIM_AWAITING;
      sim_status_ = sim_status == InstMode::DETAILED ? SIM_ROI : SIM_WARMUP;
   }
}

void DRAMsimCntlr::stop(){
   if (status_ == DRAMSIM_NOT_STARTED) return ; // do nothing
   // Finish pending dram requests.
   fprintf(stderr, "[DRAMSIM #%d] Trying to finish %lu pending reqs, %lu in-flight reqs\n", ch_id, pending_reqs_.size(), in_flight_reqs_.size());
   printPendingReqs();
   while(in_flight_reqs_.size() > 0){ // If there are any pending requests and in_flight_reqs. runDRAMsim till finish all. 
      runDRAMsim(clk_ + epoch_size);
      for (auto it = in_flight_reqs_.begin(); it != in_flight_reqs_.end(); it++){
         LOG_ASSERT_ERROR(it->second + 10000 >= clk_, "[DRAMSIM #%d] clk_ %lu Long in-flight request 0x%lX @clk %lu\n", ch_id, clk_, it->first, it->second);
      }
   }
   status_ = DRAMSIM_DONE;
      fprintf(stderr, "[DRAMSIM #%d] Finish DRAMsim3. Latest Request %ld ns @cycel %ld, clk %lu\n", ch_id, t_latest_req_.getNS(), clk_latest_req_, clk_);
      fprintf(stderr, "[DRAMSIM #%d] avg DRAMsim3 latency %f clk \n", ch_id, (float)total_read_lat / num_of_reads);
   read_lat_generator.print_stats(stderr);
   mem_system_->PrintStats();
}

void DRAMsimCntlr::printPendingReqs(){
   fprintf(stderr, "%s[DRAMSIM #%d] @clk %lu: In Flight Reqs: \n", "", ch_id, clk_);
   for (auto it = in_flight_reqs_.begin(); it != in_flight_reqs_.end(); it++){
      fprintf(stderr, "0x%012lX %s %lu\n", it->first & (~TRANS_IS_WRITE_MASK), it->first & TRANS_IS_WRITE_MASK ? "WRITE":"READ", it->second);
   }
   fprintf(stderr, "Pending Request: \n");
   for (auto it = pending_reqs_.begin(); it != pending_reqs_.end(); it++){
      fprintf(stderr, "0x%012lX %s %lu\n", it->second & (~TRANS_IS_WRITE_MASK), it->second & TRANS_IS_WRITE_MASK ?"WRITE":"READ", it->first);
   }
}

DRAMsimCntlr::LatencyGenerator::LatencyGenerator(String config_prefix){
   std::srand(0);
   window_size = Sim()->getCfg()->getInt(config_prefix + "window_size");
}

void DRAMsimCntlr::LatencyGenerator::add_latency(SubsecondTime latency){
   read_latencies.push_back(latency);
   while (read_latencies.size() > window_size){
      read_latencies.pop_front();
   }
}

SubsecondTime DRAMsimCntlr::LatencyGenerator::get_latency(){
   int rand_pos = std::rand() % read_latencies.size();
   total_lat += read_latencies[rand_pos];
   ++num_access;
   return read_latencies[rand_pos];
}

void DRAMsimCntlr::LatencyGenerator::print_latencies(FILE* file){
   // for (auto it = read_latencies.begin(); it != read_latencies.end(); it++){
   //    fprintf(file, "%lu\t", it->getNS());
   // }
   // fprintf(file, "\n");
}

void DRAMsimCntlr::LatencyGenerator::print_stats(FILE* file){
   fprintf(file, "avg simluation latency %lu ns\n", (total_lat / num_access).getNS());
}