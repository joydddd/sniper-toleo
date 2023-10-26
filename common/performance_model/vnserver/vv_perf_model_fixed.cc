#include "vv_perf_model_fixed.h"
#include "simulator.h"
#include "config.hpp"
#include "stats.h"
#include "shmem_perf.h"

#if 0
#define MYTRACE_ENABLED
   extern Lock iolock;
#include "core_manager.h"
#include "simulator.h"
#define MYTRACE(...)                                    \
{                                                       \
    ScopedLock l(iolock);                               \
    fflush(f_trace);                                    \
    fprintf(f_trace, "[VV] ");          \
    fprintf(f_trace, __VA_ARGS__);                      \
    fprintf(f_trace, "\n");                             \
    fflush(f_trace);                                    \
}
#else
#define MYTRACE(...) {}
#endif

VVPerfModelFixed::VVPerfModelFixed(cxl_id_t cxl_id, UInt32 vn_size)
    : VVPerfModel(vn_size),
    m_dram_perf_model(NULL),
    m_total_read_delay(SubsecondTime::Zero()),
    m_total_update_latency(SubsecondTime::Zero()),
    m_entry_size(512) {
        fprintf(stderr, "Create Dram perf model from vvperfmodel, cxl_id %d\n", cxl_id);
    m_dram_perf_model = DramPerfModel::createDramPerfModel(cxl_id, m_entry_size/8, DramType::CXL_VN);

    registerStatsMetric("vv", cxl_id, "total-reads", &m_num_reads);
    registerStatsMetric("vv", cxl_id, "total-updates", &m_num_updates);
    registerStatsMetric("vv", cxl_id, "total-read-delay", &m_total_read_delay);
    registerStatsMetric("vv", cxl_id, "total-update-latency", &m_total_update_latency);

#ifdef MYTRACE_ENABLED
   f_trace = fopen("vv_perf.trace", "w+");
   std::cerr << "Create Version Vault perf trace " << "vv_perf.trace" << std::endl;
#endif // MYTRACE_ENABLED
}

VVPerfModelFixed::~VVPerfModelFixed()
{
    if (m_dram_perf_model)
    {
        delete m_dram_perf_model;
        m_dram_perf_model = NULL;
    }
}

// return latency, pkt_size (in bits)
boost::tuple<SubsecondTime, UInt64> VVPerfModelFixed::getAccessLatency(
    SubsecondTime pkt_time, core_id_t requester, IntPtr address,
    CXLCntlrInterface::access_t access_type, ShmemPerf* perf) 
{
    // pkt_size is in 'bits'
    if ((!m_enabled) || (requester >= (core_id_t) Config::getSingleton()->getApplicationCores()))
        return boost::make_tuple<SubsecondTime, UInt64>(SubsecondTime::Zero(),  m_entry_size);

    SubsecondTime m_dram_latency = m_dram_perf_model->getAccessLatency(pkt_time, m_entry_size, requester, address, DramCntlrInterface::READ, perf);
    if (access_type == CXLCntlrInterface::VN_UPDATE){
        // write after read for vn updates
        m_dram_perf_model->getAccessLatency(pkt_time + m_dram_latency, m_entry_size, requester, address, DramCntlrInterface::WRITE, perf);
    }
    SubsecondTime access_latency = m_dram_latency;
    switch(access_type){
        case CXLCntlrInterface::VN_READ:
            MYTRACE("0x%016lx\tREAD\t%lu", address, access_latency.getNS());
            m_num_reads++;
            m_total_read_delay += access_latency;
            break;
        case CXLCntlrInterface::VN_UPDATE:
            MYTRACE("0x%016lx\tUPDATE\t%lu", address, access_latency.getNS());
            m_num_updates++;
            m_total_update_latency += access_latency;
            break;
        default:
            LOG_ASSERT_ERROR(false, "Unsupported access type %d on VV", access_type);
            break;
    }
    return boost::make_tuple<SubsecondTime, UInt64>(access_latency,  m_entry_size);;
}

void VVPerfModelFixed::enable(){
    m_enabled = true;
    m_dram_perf_model->enable();
}

void VVPerfModelFixed::disable(){
    m_enabled = false;
    m_dram_perf_model->disable();
}
