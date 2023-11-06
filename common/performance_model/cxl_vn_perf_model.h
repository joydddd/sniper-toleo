#ifndef __CXL_VN_PERF_MODEL_H__
#define __CXL_VN_PERF_MODEL_H__

#include "cxl_perf_model.h"
#include "queue_model.h"
#include "fixed_types.h"
#include "subsecond_time.h"
#include "cxl_cntlr_interface.h"
#include "vv_perf_model.h"

class CXLVNPerfModel : public CXLPerfModel
{
   private:
      QueueModel* m_queue_model;
      SubsecondTime m_cxl_access_cost;
      ComponentBandwidth m_cxl_bandwidth;
      
      SubsecondTime m_total_queueing_delay;
      SubsecondTime m_total_access_latency;
 
      VVPerfModel* m_vv_perf_model;


     public:
      CXLVNPerfModel(cxl_id_t cxl_id, UInt64 cache_block_size /* in bits */);
      ~CXLVNPerfModel();
      SubsecondTime getAccessLatency(SubsecondTime pkt_time, UInt64 pkt_size,
                                     core_id_t requester, IntPtr address,
                                     CXLCntlrInterface::access_t access_type,
                                     ShmemPerf* perf);
      void enable();
      void disable();

      UInt64 getTotalAccesses() { return m_num_accesses; }
};

#endif // __CXL_VN_PERF_MODEL_H__