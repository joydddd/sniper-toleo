#ifndef __VV_PERF_MODEL_FIXED__
#define __VV_PERF_MODEL_FIXED__

#include "vv_perf_model.h"
#include "queue_model.h"
#include "subsecond_time.h"
#include "fstream"
#include "dram_perf_model.h"
#include "common.h" // DRAMsim3


class VVPerfModelFixed : public VVPerfModel
{
    private:
    DramPerfModel* m_dram_perf_model;
    SubsecondTime m_total_read_delay;
    SubsecondTime m_total_update_latency;

    UInt32 m_entry_size; // size of each entry in the version number table, in bits

    std::ifstream if_trace_; // input trace file
    dramsim3::Transaction pending_trasaction; // pending transaction

    public:
     VVPerfModelFixed(cxl_id_t cxl_id, UInt32 vn_size);
     ~VVPerfModelFixed();
     boost::tuple<SubsecondTime, UInt64> getAccessLatency(
         SubsecondTime pkt_time, core_id_t requester, IntPtr address,
         CXLCntlrInterface::access_t access_type, ShmemPerf* perf);
     void enable();
     void disable();
};

#endif /* __VV_PERF_MODEL_CONSTANT__ */