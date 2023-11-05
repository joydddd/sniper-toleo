#ifndef __VV_PERF_MODEL_1STEP__
#define __VV_PERF_MODEL_1STEP__

#include "vv_perf_model.h"
#include "queue_model.h"
#include "subsecond_time.h"
#include "fstream"
#include  "dram_perf_model.h"
#include "common.h"


class VN_Page {
    public:
    typedef enum { ONE_STEP = 16, VAULT = 128, OVERFLOW = 512} vn_comp_t;

    VN_Page(){}
    ~VN_Page(){}

    vn_comp_t update(UInt8 cl_num); // update Vault Page entry type
    vn_comp_t get_type() { return type; }
    UInt32 reset(); // return number of CLs to re-encrypt

    private:
    vn_comp_t type = ONE_STEP;
    UInt8 vn_private[64] = {0};
    UInt32 m_total_offset = 0; // sum of vn_private. 
};


class VVPerfModel1Step : public VVPerfModel
{
    private:
    DramPerfModel* m_dram_perf_model;
    SubsecondTime m_total_read_delay;
    SubsecondTime   m_total_update_latency;
    std::unordered_map<IntPtr, VN_Page> m_vault_pages;

    std::ifstream if_trace_; // input trace file
    dramsim3::Transaction pending_trasaction; // pending transaction

    public:
     VVPerfModel1Step(cxl_id_t cxl_id, UInt32 vn_size);
     ~VVPerfModel1Step();
     boost::tuple<SubsecondTime, UInt64> getAccessLatency(
         SubsecondTime pkt_time, core_id_t requester, IntPtr address,
         CXLCntlrInterface::access_t access_type, ShmemPerf* perf);
     void enable();
     void disable();
};


#endif /* __VV_PERF_MODEL_1STEP__ */