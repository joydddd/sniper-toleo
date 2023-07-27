#ifndef __CXL_VN_PERF_MODEL_DRAMSIM_H__
#define __CXL_VN_PERF_MODEL_DRAMSIM_H__

#include "cxl_perf_model.h"
#include "queue_model.h"
#include "fixed_types.h"
#include "subsecond_time.h"
#include "cxl_cntlr_interface.h"

class CXLVNPerfModelDramSim : public CXLPerfModel
{

     public:
      CXLVNPerfModelDramSim(cxl_id_t cxl_id, UInt64 transaction_size /* in bits */);
      ~CXLVNPerfModelDramSim();
      SubsecondTime getAccessLatency(SubsecondTime pkt_time, UInt64 pkt_size,
                                     core_id_t requester, IntPtr address,
                                     CXLCntlrInterface::access_t access_type,
                                     ShmemPerf* perf);
};

#endif // __CXL_VN_PERF_MODEL_DRAMSIM_H__