#ifndef __CXL_PERF_MODEL_H__
#define __CXL_PERF_MODEL_H__

#include "queue_model.h"
#include "fixed_types.h"
#include "subsecond_time.h"
#include "cxl_cntlr_interface.h"

class CXLPerfModel
{
   private:
      bool m_enabled;
      UInt64 m_num_accesses;

      String m_cxl_id_str;
      cxl_id_t m_cxl_id;

      QueueModel* m_queue_model;
      SubsecondTime m_cxl_access_cost;
      ComponentBandwidth m_cxl_bandwidth;
      
      SubsecondTime m_total_queueing_delay;
      SubsecondTime m_total_access_latency;


     public:
      CXLPerfModel(cxl_id_t cxl_id, UInt64 cache_block_size);
      ~CXLPerfModel();
      SubsecondTime getAccessLatency(SubsecondTime pkt_time, UInt64 pkt_size,
                                     core_id_t requester, IntPtr address,
                                     CXLCntlrInterface::access_t access_type,
                                     ShmemPerf* perf);
      void enable() { m_enabled = true; }
      void disable() { m_enabled = false; }

      UInt64 getTotalAccesses() { return m_num_accesses; }
};

#endif // __CXL_PERF_MODEL_H__