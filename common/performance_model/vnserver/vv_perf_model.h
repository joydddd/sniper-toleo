#ifndef __VV_PERF_MODEL__
#define __VV_PERF_MODEL__

#include "subsecond_time.h"
#include "cxl_cntlr_interface.h" // for access types

class ShmemPerf;

class VVPerfModel {
    protected:
     bool m_enabled;
     UInt64 m_num_reads, m_num_updates;
     FILE* f_trace;
    public:
     static VVPerfModel* createVVPerfModel(cxl_id_t cxl_id, UInt32 vn_size); // version number length in bits

     VVPerfModel(UInt32 vn_size) : m_enabled(false), m_num_reads(0), m_num_updates(0) {}
     virtual ~VVPerfModel() {}

     /* return Access latency and pkt size */
     virtual boost::tuple<SubsecondTime, UInt64> getAccessLatency(
         SubsecondTime pkt_time, core_id_t requester, IntPtr address,
         CXLCntlrInterface::access_t access_type, ShmemPerf* perf) = 0;
     virtual void enable() { m_enabled = true; }
     virtual void disable() { m_enabled = false; }

     UInt64 getTotalReads() { return m_num_reads; }
     UInt64 getTotalUpdates() { return m_num_updates; }
};

#endif /* __VV_PERF_MODEL__ */