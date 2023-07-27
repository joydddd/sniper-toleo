#ifndef __CXL_PERF_MODEL_CONST_H__
#define __CXL_PERF_MODEL_CONST_H__
#include "cxl_perf_model.h"
#include "queue_model.h"
#include "fixed_types.h"
#include "subsecond_time.h"
#include "cxl_cntlr_interface.h"

class CXLPerfModelConstant : public CXLPerfModel
{
   private:
      QueueModel* m_queue_model;
      SubsecondTime m_cxl_access_cost;
      ComponentBandwidth m_cxl_bandwidth;
      
      SubsecondTime m_total_queueing_delay;
      SubsecondTime m_total_access_latency;

     public:
      CXLPerfModelConstant(cxl_id_t cxl_id, UInt64 transaction_size /* in bits */);
      ~CXLPerfModelConstant();
      SubsecondTime getAccessLatency(SubsecondTime pkt_time, UInt64 pkt_size,
                                     core_id_t requester, IntPtr address,
                                     CXLCntlrInterface::access_t access_type,
                                     ShmemPerf* perf);
};

#endif // __CXL_PERF_MODEL_CONST_H__