#ifndef __DRAM_PERF_MODEL_DRAMSIM_H__
#define __DRAM_PERF_MODEL_DRAMSIM_H__

#include "dram_perf_model.h"
#include "queue_model.h"
#include "fixed_types.h"
#include "subsecond_time.h"
#include "dram_cntlr_interface.h"
#include "memory_system.h" // DRAMsim3
#include <map>

class DramPerfModelDramSim;

class DRAMsimCntlr{
   private:
    uint64_t epoch_size; // cycles in one epoch
    ComponentPeriod dram_period;
    uint32_t ch_id;

    enum DramSimStatus{
     DRAMSIM_IDEL = 0,
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

    DramPerfModelDramSim* perf_model_;


    void ReadCallBack(uint64_t addr);
    void WriteCallBack(uint64_t addr);
    void runDRAMsim(uint64_t target_cycle);
   public:
      DRAMsimCntlr(DramPerfModelDramSim* perf_model, uint32_t ch_id);
      ~DRAMsimCntlr();
      void start();
      void stop();
      void addTrans(SubsecondTime t_sniper, IntPtr addr, bool is_write);
};

class DramPerfModelDramSim : public DramPerfModel {
   private:
      QueueModel* m_queue_model;
      SubsecondTime m_dram_access_cost;
      ComponentBandwidth m_dram_bandwidth;
      UInt64 m_cache_block_size;

      SubsecondTime m_total_queueing_delay;
      SubsecondTime m_total_access_latency;

      // DRAMsim3
      DRAMsimCntlr** m_dramsim;
      UInt32 m_dramsim_channels;
      
      ShmemPerfModel* m_shmem_perf_model;
      ShmemPerfModel* getShmemPerfModel() { return m_shmem_perf_model; }

      void dramsimReadCallBack(uint64_t addr);
      void dramsimWriteCallBack(uint64_t addr);

      void dramsimStart();
      void dramsimEnd();

     public:
      DramPerfModelDramSim(core_id_t core_id, UInt32 cache_block_size);

      ~DramPerfModelDramSim();

      SubsecondTime getAccessLatency(SubsecondTime pkt_time, UInt64 pkt_size, core_id_t requester, IntPtr address, DramCntlrInterface::access_t access_type, ShmemPerf *perf);

      static SInt64 ROIstartHOOK(UInt64 object, UInt64 argument){
         ((DramPerfModelDramSim*)object)->dramsimStart();
         return 0;
      }

      static SInt64 ROIendHOOK(UInt64 object, UInt64 argument){
         ((DramPerfModelDramSim*)object)->dramsimEnd();
         return 0;
      }
      friend class DRAMsimCntlr;
};
#endif /* __DRAM_PERF_MODEL_DRAMSIM_H__ */
