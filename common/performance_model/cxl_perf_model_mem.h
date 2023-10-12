#ifndef __CXL_PERF_MODEL_CONST_H__
#define __CXL_PERF_MODEL_CONST_H__
#include "cxl_perf_model.h"
#include "queue_model.h"
#include "fixed_types.h"
#include "subsecond_time.h"
#include "cxl_cntlr_interface.h"
#include "dram_perf_model.h"

class CXLPerfModelMemoryExpander : public CXLPerfModel
{
   private:
      QueueModel* m_queue_model;
      SubsecondTime m_cxl_access_cost;
      ComponentBandwidth m_cxl_bandwidth;
      
      SubsecondTime m_total_queueing_delay;
      SubsecondTime m_total_access_latency;

      DramPerfModel* m_dram_perf_model;

     public:
      CXLPerfModelMemoryExpander(cxl_id_t cxl_id, UInt64 transaction_size /* in bits */, DramPerfModel* dram_perf_model);
      ~CXLPerfModelMemoryExpander();
      SubsecondTime getAccessLatency(SubsecondTime pkt_time, UInt64 pkt_size,
                                     core_id_t requester, IntPtr address,
                                     CXLCntlrInterface::access_t access_type,
                                     ShmemPerf* perf);
      void enable() { m_enabled = true; m_dram_perf_model->enable();}
      void disable() { m_enabled = false; m_dram_perf_model->disable();}
};

#endif // __CXL_PERF_MODEL_CONST_H__