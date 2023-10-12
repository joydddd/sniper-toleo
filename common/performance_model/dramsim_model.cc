#include "dramsim_model.h"
#include "simulator.h"
#include "config.h"
#include "config.hpp"
#include <cstdlib>

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

DRAMsimCntlr::DRAMsimCntlr(uint32_t _dram_cntlr_id, uint32_t _ch_id, SubsecondTime default_latency, bool is_cxl, bool _log_trace):
   epoch_size(is_cxl ? Sim()->getCfg()->getInt("perf_model/cxl/memory_expander_" + itostr((unsigned int)_dram_cntlr_id) + "/dramsim/epoch") : Sim()->getCfg()->getInt("perf_model/dram/dramsim/epoch")),
   dram_period(is_cxl ? ComponentPeriod::fromFreqHz(Sim()->getCfg()->getFloat("perf_model/cxl/memory_expander_" + itostr((unsigned int)_dram_cntlr_id) + "/dramsim/frequency") * 1000000) 
   : ComponentPeriod::fromFreqHz(Sim()->getCfg()->getFloat("perf_model/dram/dramsim/frequency") * 1000000)), // MHz to Hz
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
   f_trace(NULL)
{
   std::string config(is_cxl ? Sim()->getCfg()->getString("perf_model/cxl/memory_expander_" + itostr((unsigned int)_dram_cntlr_id) + "/dramsim/config").c_str() 
                              :Sim()->getCfg()->getString("perf_model/dram/dramsim/config").c_str());
   std::string output_dir(is_cxl ? Sim()->getCfg()->getString("perf_model/cxl/memory_expander_" + itostr((unsigned int)_dram_cntlr_id) + "/dramsim/output_dir").c_str()
                                 :Sim()->getCfg()->getString("perf_model/dram/dramsim/output_dir").c_str());
   if (is_cxl) output_dir.append("/dramsim_cxl" + std::to_string(dram_cntlr_id) + "_" + std::to_string(ch_id));
   else  output_dir.append("/dramsim_" + std::to_string(dram_cntlr_id) + "_" + std::to_string(ch_id));
   std::system(("mkdir -p " + output_dir).c_str());

   mem_system_ = new dramsim3::MemorySystem(
       config, output_dir,
       std::bind(&DRAMsimCntlr::ReadCallBack, this, std::placeholders::_1),
       std::bind(&DRAMsimCntlr::WriteCallBack, this, std::placeholders::_1));
   read_lat_generator.add_latency(default_latency);

#ifdef MYTRACE_ENABLED
   if (log_trace){
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

void DRAMsimCntlr::runDRAMsim(uint64_t target_cycle){ // run DRAMsim until cycle count
   // run DRAMsim until 
   auto it = pending_reqs_.begin();
   for (; clk_ < target_cycle; clk_++) {
      mem_system_->ClockTick();
      if (it != pending_reqs_.end() && it->first <= clk_){ // req cycle <= current clk
         bool get_next = mem_system_->WillAcceptTransaction(it->second.addr, it->second.is_write);
         if (get_next) {
            // fprintf(stderr, "[DRAMSIM #%d] @0x%016lx send Req %ld [%ld]cycle \n", ch_id, it->second.addr,  it->first, clk_);
            mem_system_->AddTransaction(it->second.addr, it->second.is_write);
            MYTRACE("0x%lX\t%s\t%lu", it->second.addr, it->second.is_write ? "WRITE":"READ", it->first);
            in_flight_reqs_.insert(std::make_pair(it->second.addr, clk_));
            it = pending_reqs_.erase(it);
         }
      }
   }
}

void DRAMsimCntlr::ReadCallBack(uint64_t addr){
   auto req_range = in_flight_reqs_.equal_range(addr);
   auto req_it = req_range.first;
   uint64_t req_clk = req_it->second;
   in_flight_reqs_.erase(req_it);
   CALLBACKTRACE("0x%lX\tREAD\t%lu\t%lu", addr, req_clk, clk_-req_clk);
   total_read_lat += clk_ - req_clk;
   num_of_reads++;
   read_lat_generator.add_latency((clk_ - req_clk) * dram_period.getPeriod());
}

void DRAMsimCntlr::WriteCallBack(uint64_t addr) {
   auto req_range = in_flight_reqs_.equal_range(addr);
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
   uint64_t req_clk =  (t_sniper - t_start_).getInternalDataForced() / dram_period.getPeriod().getInternalDataForced();
   LOG_ASSERT_ERROR(req_clk >= clk_, "DRAM Request outside of Epoch: req_clk %ld, clk_ %ld", req_clk, clk_);
   t_latest_req_ = t_sniper > t_latest_req_ ? t_sniper : t_latest_req_;
   clk_latest_req_ = req_clk > clk_latest_req_ ? req_clk : clk_latest_req_;
//    fprintf(stderr, "[DRAMSIM #%d] @0x%016lx Add Req %ld cycle t %ld \n", ch_id, addr, req_clk, t_sniper.getNS());
   dramsim3::Transaction trans(addr, is_write);
   pending_reqs_.insert(std::pair<uint64_t, dramsim3::Transaction>(req_clk, trans));

   // runDRAMsim(clk_latest_req_ > epoch_size ? clk_latest_req_ - epoch_size : 0);

   return read_lat_generator.get_latency();
}

void DRAMsimCntlr::advance(SubsecondTime t_barrier){
   if (status_ == DRAMSIM_NOT_STARTED || status_ == DRAMSIM_DONE) return; // Do nothing
   if (status_ == DRAMSIM_AWAITING){ // set t_start
      LOG_ASSERT_ERROR(clk_ == 0, "clk_ %lu", clk_);
      t_start_ = t_barrier;
      status_ = DRAMSIM_RUNNING;
      fprintf(stderr, "[DRAMSIM #%d] First Barrier at %ld ns, clk %lu\n", ch_id, t_barrier.getNS(), clk_);
   }
   if (status_ == DRAMSIM_RUNNING){ // run DRAMsim
      uint64_t barrier_clk =  (t_barrier - t_start_).getInternalDataForced() / dram_period.getPeriod().getInternalDataForced();
      read_lat_generator.newEpoch();
      runDRAMsim(barrier_clk);
      // fprintf(stderr, "[DRAMSIM #%d] Barrier at %ld ns, clk %lu\n", ch_id, t_barrier.getNS(), barrier_clk);
   }
}

void DRAMsimCntlr::start(InstMode::inst_mode_t sim_status){
   LOG_ASSERT_ERROR(status_ != DRAMSIM_DONE, "DRAMSim3 has already fninshed running");
   if (status_ != DRAMSIM_NOT_STARTED){
      LOG_ASSERT_ERROR(sim_status_ == SIM_WARMUP, "DRAMsim3 did not enter warmup mode");
      sim_status_ = SIM_ROI;
      fprintf(stderr, "[DRAMSIM #%d] Enter ROI %lu\n", ch_id, clk_);
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
   while(in_flight_reqs_.size() > 0){
      runDRAMsim(clk_ + epoch_size);
   }
   status_ = DRAMSIM_DONE;
   fprintf(stderr, "[DRAMSIM #%d] Finish DRAMsim3. Latest Request %ld ns @cycel %ld, clk %lu\n", ch_id, t_latest_req_.getNS(), clk_latest_req_, clk_);
   fprintf(stderr, "[DRAMSIM #%d] avg DRAMsim3 latency %f clk \n", ch_id, (float)total_read_lat / num_of_reads);
   read_lat_generator.print_stats(stderr);
   mem_system_->PrintStats();
}


DRAMsimCntlr::LatencyGenerator::LatencyGenerator(){
   std::srand(0);
}

void DRAMsimCntlr::LatencyGenerator::newEpoch(){
   new_epoch = true;
}

void DRAMsimCntlr::LatencyGenerator::add_latency(SubsecondTime latency){
   if (new_epoch){
      read_latencies.clear();
      new_epoch = false;
   }
   read_latencies.push_back(latency);
}

SubsecondTime DRAMsimCntlr::LatencyGenerator::get_latency(){
   int rand_pos = std::rand() % read_latencies.size();
   // fprintf(stderr, "latency %lu\n", read_latencies[rand_pos].getNS());
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