#ifndef __CXL_PERF_MODEL_NAIVE_H__
#define __CXL_PERF_MODEL_NAIVE_H__
#include "cxl_perf_model.h"
#include "queue_model.h"
#include "fixed_types.h"
#include "subsecond_time.h"
#include "cxl_cntlr_interface.h"
#include "dram_perf_model.h"

// Naive CXL Perf Model. Does not model DRAM latency. 

class CXLPerfModelNaive : public CXLPerfModel
{
   private:
      QueueModel* m_queue_model;
      SubsecondTime m_cxl_access_cost;
      ComponentBandwidth m_cxl_bandwidth;
      
      SubsecondTime m_total_queueing_delay;
      SubsecondTime m_total_channel_latency;
      UInt64 m_total_bytes_trans;

     public:
      CXLPerfModelNaive(cxl_id_t cxl_id, UInt64 transaction_size /* in bits */);
      ~CXLPerfModelNaive();
      SubsecondTime getAccessLatency(SubsecondTime pkt_time, UInt64 pkt_size,
                                     core_id_t requester, IntPtr address,
                                     CXLCntlrInterface::access_t access_type,
                                     ShmemPerf* perf);
      void enable() { m_enabled = true;}
      void disable() { m_enabled = false; }
};

#endif // __CXL_PERF_MODEL_CONST_H__