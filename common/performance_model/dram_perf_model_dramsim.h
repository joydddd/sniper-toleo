#ifndef __DRAM_PERF_MODEL_DRAMSIM_H__
#define __DRAM_PERF_MODEL_DRAMSIM_H__

#include "dram_perf_model.h"
#include "queue_model.h"
#include "fixed_types.h"
#include "subsecond_time.h"
#include "dram_cntlr_interface.h"
#include "dramsim_model.h"
#include "queue_model.h"


class DramPerfModelDramSim : public DramPerfModel {
   private:
      String m_config_prefix;
      SubsecondTime m_dram_access_cost;
      UInt64 m_cache_block_size;

      SubsecondTime m_total_access_latency;
      SubsecondTime m_total_bp_latency;
      SubsecondTime m_total_read_latency;

      // DRAMsim3
      DRAMsimCntlr** m_dramsim;
      QueueModel* m_backpressure_queue;
      UInt32 m_dramsim_channels;
      SubsecondTime m_dramsim_buffering_delay;
      SubsecondTime m_burst_processing_time;
      float m_bp_factor;
      UInt32 m_dram_burst_size;

      ShmemPerfModel* m_shmem_perf_model;
      ShmemPerfModel* getShmemPerfModel() { return m_shmem_perf_model; }

      void dramsimReadCallBack(uint64_t addr);
      void dramsimWriteCallBack(uint64_t addr);

      void dramsimStart(InstMode::inst_mode_t sim_status);
      void dramsimEnd();
      void dramsimAdvance(SubsecondTime barrier_time);

     public:
      DramPerfModelDramSim(core_id_t core_id, UInt32 cache_block_size, DramType dram_type = DramType::SYSTEM_DRAM);

      ~DramPerfModelDramSim();

      SubsecondTime getAccessLatency(SubsecondTime pkt_time, UInt64 pkt_size, core_id_t requester, IntPtr address, DramCntlrInterface::access_t access_type, ShmemPerf *perf);

      static SInt64 Periodic_HOOK(UInt64 object, UInt64 barrier_time){
         subsecond_time_t t; t.m_time = barrier_time;
         ((DramPerfModelDramSim*)object)->dramsimAdvance(SubsecondTime(t));
         return 0;
      }
      
      
      static SInt64 Change_mode_HOOK(UInt64 object, UInt64 mode){
         if (mode == InstMode::DETAILED || mode == InstMode::CACHE_ONLY){ // start when warmup region or ROI starts
            ((DramPerfModelDramSim*)object)->dramsimStart((InstMode::inst_mode_t)mode);
         } if (mode == InstMode::FAST_FORWARD){
            ((DramPerfModelDramSim*)object)->dramsimEnd();
         }
         return 0;
      }

      friend class DRAMsimCntlr;
};
#endif /* __DRAM_PERF_MODEL_DRAMSIM_H__ */
