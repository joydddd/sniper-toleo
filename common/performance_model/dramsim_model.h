#ifndef __DRAMSIM_MODEL_H__
#define __DRAMSIM_MODEL_H__

#include "fixed_types.h"
#include "subsecond_time.h"
#include "memory_system.h" // DRAMsim3
#include <map>

class DramPerfModelDramSim;

class DRAMsimCntlr{
   private:
    uint64_t epoch_size; // cycles in one epoch
    ComponentPeriod dram_period;
    SubsecondTime epoch_period;// period of one epoch
    uint32_t ch_id, dram_cntlr_id;

    bool log_trace = false;

    enum DramSimStatus{
     DRAMSIM_NOT_STARTED = 0,
     DRAMSIM_AWAITING,
     DRAMSIM_RUNNING,
     DRAMSIM_DONE
    } status_;
    dramsim3::MemorySystem *mem_system_;
    uint64_t clk_;
    SubsecondTime t_start_;
    SubsecondTime t_latest_req_;
    uint64_t clk_latest_req_;
    std::map<uint64_t, dramsim3::Transaction> pending_reqs_;

    FILE* f_trace; 

    void ReadCallBack(uint64_t addr);
    void WriteCallBack(uint64_t addr);
    void runDRAMsim(uint64_t target_cycle);
   public:
      DRAMsimCntlr(uint32_t dram_cntlr_id, uint32_t ch_id, bool is_cxl = false, bool log_trace = false);
      ~DRAMsimCntlr();
      void start();
      void stop();
      void advance(SubsecondTime t_barrier);
      void addTrans(SubsecondTime t_sniper, IntPtr addr, bool is_write);
};


#endif // __DRAMSIM_MODEL_H__