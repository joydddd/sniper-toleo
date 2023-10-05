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
{ if (log_trace && status_ == DRAMSIM_RUNNING){                      \
    ScopedLock l(iolock);                               \
    fflush(f_trace);                                    \
    fprintf(f_trace, __VA_ARGS__);                      \
    fprintf(f_trace, "\n");                             \
    fflush(f_trace);                                    \
}}
#else
#define MYTRACE(...) {}
#endif

DRAMsimCntlr::DRAMsimCntlr(uint32_t _dram_cntlr_id, uint32_t _ch_id, bool is_cxl, bool _log_trace):
   epoch_size(is_cxl ? Sim()->getCfg()->getInt("perf_model/cxl/memory_expander_" + itostr((unsigned int)_dram_cntlr_id) + "/dramsim/epoch") : Sim()->getCfg()->getInt("perf_model/dram/dramsim/epoch")),
   dram_period(is_cxl ? ComponentPeriod::fromFreqHz(Sim()->getCfg()->getFloat("perf_model/cxl/memory_expander_" + itostr((unsigned int)_dram_cntlr_id) + "/dramsim/frequency") * 1000000) 
   : ComponentPeriod::fromFreqHz(Sim()->getCfg()->getFloat("perf_model/dram/dramsim/frequency") * 1000000)), // MHz to Hz
   ch_id(_ch_id),
   dram_cntlr_id(_dram_cntlr_id),
   log_trace(_log_trace),
   status_(DRAMSIM_IDEL),
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
   
#ifdef MYTRACE_ENABLED
   if (log_trace)
    f_trace = fopen(("dramsim_" + std::to_string(dram_cntlr_id) + "_" + std::to_string(ch_id) + ".trace").c_str(), "w+");
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
      if (it == pending_reqs_.end()) {
         status_ = DRAMSIM_DONE;
         return;
      }
      mem_system_->ClockTick();
      if (it->first <= clk_){ // req cycle <= current clk
         bool get_next = mem_system_->WillAcceptTransaction(it->second.addr, it->second.is_write);
         if (get_next) {
            // fprintf(stderr, "[DRAMSIM #%d] @0x%016lx send Req %ld [%ld]cycle \n", ch_id, it->second.addr,  it->first, clk_);
            mem_system_->AddTransaction(it->second.addr, it->second.is_write);
            MYTRACE("0x%lX\t%s\t%lu", it->second.addr, it->second.is_write ? "WRITE":"READ", it->first);
            it = pending_reqs_.erase(it);
         }
      }
   }
}

void DRAMsimCntlr::ReadCallBack(uint64_t addr){
//    fprintf(stderr, "[DRAMSIM #%d] R callback @ %016lx clk %lu\n", ch_id, addr, clk_);
}

void DRAMsimCntlr::WriteCallBack(uint64_t addr) {
//    fprintf(stderr, "[DRAMSIM #%d] W callback @ %016lx clk %lu\b", ch_id, addr, clk_);
}

void DRAMsimCntlr::addTrans(SubsecondTime t_sniper, IntPtr addr, bool is_write){
   if (status_ == DRAMSIM_IDEL || status_ == DRAMSIM_DONE){
      return;
   }

   // set t_start_ if this is the first transaction
   if (status_ == DRAMSIM_AWAITING){
      LOG_ASSERT_ERROR(clk_ == 0, "clk_ %lu", clk_);
      t_start_ = t_sniper > epoch_size * dram_period.getPeriod() ? t_sniper - epoch_size * dram_period.getPeriod() : SubsecondTime::Zero(); 
      // t_start = first_req_time - epoch_size
      status_ = DRAMSIM_RUNNING;
      fprintf(stderr, "[DRAMSIM #%d] First Req at %ld ns, t0 = %ld ns clk %lu\n", ch_id, t_sniper.getNS(), t_start_.getNS(), clk_);
   }
   // DRAMSIM_RUNNING
   LOG_ASSERT_ERROR(t_sniper > t_start_, "t_sniper %ld, t_start %ld", t_sniper.getNS(), t_start_.getNS());
   uint64_t req_clk =  (t_sniper - t_start_).getInternalDataForced() / dram_period.getPeriod().getInternalDataForced();
   t_latest_req_ = t_sniper > t_latest_req_ ? t_sniper : t_latest_req_;
   clk_latest_req_ = req_clk > clk_latest_req_ ? req_clk : clk_latest_req_;
//    fprintf(stderr, "[DRAMSIM #%d] @0x%016lx Add Req %ld cycle t %ld \n", ch_id, addr, req_clk, t_sniper.getNS());
   dramsim3::Transaction trans(addr, is_write);
   pending_reqs_.insert(std::pair<uint64_t, dramsim3::Transaction>(req_clk, trans));
   
   runDRAMsim(clk_latest_req_ - epoch_size);
}

void DRAMsimCntlr::start(){
   LOG_ASSERT_ERROR(status_ == DRAMSIM_IDEL, "DRAMSim3 has already been started %d", status_);
   clk_ = 0;
   status_ = DRAMSIM_AWAITING;
//    fprintf(stderr, "[DRAMSIM #%d] Start DRAMsim3 at %ld ns, clk %lu\n", ch_id, t_start_.getNS(), clk_);
}

void DRAMsimCntlr::stop(){
   LOG_ASSERT_ERROR(status_ == DRAMSIM_RUNNING, "DRAMSim3 is not running %d", status_);
   // Finish pending dram requests. 
   while(status_ != DRAMSIM_DONE){
      runDRAMsim(clk_ + epoch_size);
   }
   fprintf(stderr, "[DRAMSIM #%d] Finish DRAMsim3. Latest Request %ld ns @cycel %ld, clk %lu\n", ch_id, t_latest_req_.getNS(), clk_latest_req_, clk_);
   mem_system_->PrintStats();
}
