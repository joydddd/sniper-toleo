#ifndef __DRAMSIM_MODEL_H__
#define __DRAMSIM_MODEL_H__

#include "fixed_types.h"
#include "subsecond_time.h"
#include "inst_mode.h"
#include "memory_system.h" // DRAMsim3
#include "dram_perf_model.h"
#include <map>

class DramPerfModelDramSim;


class DRAMsimCntlr {
   private:
    String config_prefix;
    uint64_t epoch_size;  // cycles in one epoch
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

    enum SimStatus { 
      SIM_NOT_STARTED = 0, 
      SIM_WARMUP,
      SIM_ROI,
      SIM_DONE
    } sim_status_;

    dramsim3::MemorySystem *mem_system_;
    uint64_t clk_;
    SubsecondTime t_start_;
    SubsecondTime t_latest_req_;
    uint64_t clk_latest_req_;
    std::map<uint64_t, dramsim3::Transaction> pending_reqs_;
    std::unordered_multimap<IntPtr, uint64_t> in_flight_reqs_;

    FILE* f_trace; 
    FILE* f_callback_trace;

    
   class LatencyGenerator{
      private:
      std::vector<SubsecondTime> read_latencies;
      bool new_epoch = false;
      SubsecondTime total_lat = SubsecondTime::Zero();
      uint64_t num_access = 0;
      public:
      LatencyGenerator();
      void newEpoch();
      void add_latency(SubsecondTime latency);
      void print_latencies(FILE* file);
      void print_stats(FILE* file);
      SubsecondTime get_latency();
   }read_lat_generator;

    uint64_t total_read_lat = 0;
    uint64_t num_of_reads = 0;

    void ReadCallBack(uint64_t addr);
    void WriteCallBack(uint64_t addr);
    void runDRAMsim(uint64_t target_cycle);
   public:
      DRAMsimCntlr(uint32_t dram_cntlr_id, uint32_t ch_id, SubsecondTime default_latency, DramType dram_type = DramType::SYSTEM_DRAM, bool log_trace = false);
      ~DRAMsimCntlr();
      void start(InstMode::inst_mode_t sim_status = InstMode::DETAILED);
      void stop();
      void advance(SubsecondTime t_barrier);
      SubsecondTime addTrans(SubsecondTime t_sniper, IntPtr addr, bool is_write);
};

#endif // __DRAMSIM_MODEL_H__