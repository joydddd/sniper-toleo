#ifndef __DRAMSIM_MODEL_H__
#define __DRAMSIM_MODEL_H__

#include "fixed_types.h"
#include "subsecond_time.h"
#include "inst_mode.h"
#include "memory_system.h" // DRAMsim3
#include "dram_perf_model.h"
#include <map>
#include <deque>

class DramPerfModelDramSim;


class DRAMsimCntlr {
   private:
    String config_prefix;
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

    enum SimStatus { 
      SIM_NOT_STARTED = 0, 
      SIM_WARMUP,
      SIM_ROI,
      SIM_DONE
    } sim_status_;

    typedef uint64_t DramTrans; // | 1 bit bool, is_write | 63 bit address |
    static const uint64_t TRANS_IS_WRITE_MASK = ((uint64_t)0xfffffff << 63);
    static const int TRANS_IS_WRITE_OFFSET = 63;

    // DEBUG:
    uint64_t check_point = 0;

    dramsim3::MemorySystem* mem_system_;
    uint64_t clk_;
    SubsecondTime t_start_;
    SubsecondTime t_latest_req_;
    uint64_t clk_latest_req_;
    std::map<uint64_t, DramTrans> pending_reqs_;
    std::unordered_multimap<DramTrans, uint64_t> in_flight_reqs_;
    uint64_t epoch_acc_late_launch, epoch_total_launched;

    FILE* f_trace; 
    FILE* f_callback_trace;

    
   class LatencyGenerator{
      private:
      std::deque<SubsecondTime> read_latencies;
      uint32_t window_size;
      SubsecondTime total_lat = SubsecondTime::Zero();
      uint64_t num_access = 0;
      public:
      LatencyGenerator(String config_prefix);
      void add_latency(SubsecondTime latency);
      void print_latencies(FILE* file);
      void print_stats(FILE* file);
      SubsecondTime get_latency();
   }read_lat_generator;

    uint64_t total_read_lat;
    uint64_t num_of_reads;

    void ReadCallBack(uint64_t addr);
    void WriteCallBack(uint64_t addr);
    uint64_t runDRAMsim(uint64_t target_cycle);// return number of memory requests issued
    void printPendingReqs();
   public:
      DRAMsimCntlr(uint32_t dram_cntlr_id, uint32_t ch_id, SubsecondTime default_latency,  DramType dram_type = DramType::SYSTEM_DRAM, bool log_trace = false);
      ~DRAMsimCntlr();
      void start(InstMode::inst_mode_t sim_status = InstMode::DETAILED);
      void stop();
      float advance(SubsecondTime t_barrier); // return updated bandwidth overflow factor
      SubsecondTime addTrans(SubsecondTime t_sniper, IntPtr addr, bool is_write);
      ComponentPeriod getDramPeriod(){return dram_period;}
      UInt32 getBurstSize(){return mem_system_->GetBurstLength();} // in bytes
      UInt32 getDramQueueSize(){return mem_system_->GetQueueSize();} // dramqueue size in number of transactions
};

#endif // __DRAMSIM_MODEL_H__