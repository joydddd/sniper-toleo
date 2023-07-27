#ifndef __CXL_PERF_MODEL_DRAMSIM_H__
#define __CXL_PERF_MODEL_DRAMSIM_H__
#include "cxl_perf_model.h"
#include "queue_model.h"
#include "fixed_types.h"
#include "subsecond_time.h"
#include "cxl_cntlr_interface.h"
#include "dramsim_model.h"

class CXLPerfModelDramSim : public CXLPerfModel
{
   private:
      QueueModel* m_queue_model;
      SubsecondTime m_cxl_latency, m_ddr_access_cost;
      ComponentBandwidth m_cxl_bandwidth;
      
      SubsecondTime m_total_queueing_delay;
      SubsecondTime m_total_access_latency;

      // DRAMsim3
      DRAMsimCntlr** m_dramsim;
      UInt32 m_dramsim_channels;

      void dramsimReadCallBack(uint64_t addr);
      void dramsimWriteCallBack(uint64_t addr);

      void dramsimStart();
      void dramsimEnd();

     public:
      CXLPerfModelDramSim(cxl_id_t cxl_id, UInt64 transaction_size /* in bits */);
      ~CXLPerfModelDramSim();
      SubsecondTime getAccessLatency(SubsecondTime pkt_time, UInt64 pkt_size,
                                     core_id_t requester, IntPtr address,
                                     CXLCntlrInterface::access_t access_type,
                                     ShmemPerf* perf);
      static SInt64 ROIstartHOOK(UInt64 object, UInt64 argument){
         ((CXLPerfModelDramSim*)object)->dramsimStart();
         return 0;
      }

      static SInt64 ROIendHOOK(UInt64 object, UInt64 argument){
         ((CXLPerfModelDramSim*)object)->dramsimEnd();
         return 0;
      }
      friend class DRAMsimCntlr;
};

#endif //  __CXL_PERF_MODEL_DRAMSIM_H__