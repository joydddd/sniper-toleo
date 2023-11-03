#ifndef __VV_PERF_MODEL_1STEP__
#define __VV_PERF_MODEL_1STEP__

#include "vv_perf_model.h"
#include "vv_cntlr.h"
#include "subsecond_time.h"
#include "fstream"
#include  "dram_perf_model.h"
#include "common.h"


class VVPerfModel1Step : public VVPerfModel
{
    private:
    DramPerfModel* m_dram_perf_model;
    VVCntlr* m_vv_cntlr;
    SubsecondTime m_total_read_delay;
    SubsecondTime  m_total_update_latency;

    std::ifstream if_trace_; // input trace file

    public:
     VVPerfModel1Step(cxl_id_t cxl_id, UInt32 vn_size, VVCntlr* vv_cntlr);
     ~VVPerfModel1Step();
     boost::tuple<SubsecondTime, UInt64> getAccessLatency(
         SubsecondTime pkt_time, core_id_t requester, IntPtr address,
         CXLCntlrInterface::access_t access_type, ShmemPerf* perf);
     void enable();
     void disable();
};


#endif /* __VV_PERF_MODEL_1STEP__ */