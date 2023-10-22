#include "cxl_vn_perf_model.h"
#include "simulator.h"
#include "config.h"
#include "config.hpp"
#include "stats.h"
#include "shmem_perf.h"
#include "vv_perf_model.h"

#if 1
#define MYTRACE_ENABLED
   extern Lock iolock;
#include "core_manager.h"
#include "simulator.h"
#define MYTRACE(...)                                    \
{                                                       \
    ScopedLock l(iolock);                               \
    fflush(f_trace);                                    \
    fprintf(f_trace, "[%d VN] ", m_cxl_id);            \
    fprintf(f_trace, __VA_ARGS__);                      \
    fprintf(f_trace, "\n");                             \
    fflush(f_trace);                                    \
}
#else
#define MYTRACE(...) {}
#endif

CXLVNPerfModel::CXLVNPerfModel(cxl_id_t cxl_id, UInt64 transaction_size /* in bits */):
    CXLPerfModel(cxl_id, transaction_size),
    m_queue_model(NULL),
    m_cxl_bandwidth(8 * Sim()->getCfg()->getFloat("perf_model/cxl/vnserver/bandwidth")),
    m_total_queueing_delay(SubsecondTime::Zero()),
    m_total_access_latency(SubsecondTime::Zero()),
    m_vv_perf_model(NULL)
{
    m_vv_perf_model = VVPerfModel::createVVPerfModel(cxl_id, transaction_size);
    m_cxl_access_cost =
        SubsecondTime::FS() *
        static_cast<uint64_t>(
            TimeConverter<float>::NStoFS(Sim()->getCfg()->getFloat(
                "perf_model/cxl/vnserver/latency")));
    m_queue_model = QueueModel::create("cxl-queue", cxl_id, Sim()->getCfg()->getString("perf_model/cxl/queue_type"),
                                       m_cxl_bandwidth.getRoundedLatency(transaction_size));
    
    registerStatsMetric("cxl", cxl_id, "total-access-latency", &m_total_access_latency);
    registerStatsMetric("cxl", cxl_id, "total-queueing-delay", &m_total_queueing_delay);

#ifdef MYTRACE_ENABLED
    std::ostringstream trace_filename;
    trace_filename << "vn_vault_perf_" << (int)m_cxl_id << ".trace";
    f_trace = fopen(trace_filename.str().c_str(), "w+");
    std::cerr << "Create VN Vault perf trace " << trace_filename.str().c_str() << std::endl;
#endif // MYTRACE_ENABLED
}

CXLVNPerfModel::~CXLVNPerfModel()
{
    if (m_queue_model)
    {
        delete m_queue_model;
        m_queue_model = NULL;
    }
    if (m_vv_perf_model)
    {
        delete m_vv_perf_model;
        m_vv_perf_model = NULL;
    }
}

SubsecondTime CXLVNPerfModel::getAccessLatency(SubsecondTime pkt_time, UInt64 pkt_size, core_id_t requester, IntPtr address, CXLCntlrInterface::access_t access_type, ShmemPerf *perf)
{
    // pkt_size is in 'bits'
    // m_dram_bandwidth is in 'Bits per clock cycle'
    if ((!m_enabled) || (requester >= (core_id_t) Config::getSingleton()->getApplicationCores()))
        return SubsecondTime::Zero();

    SubsecondTime vv_latency;
    boost::tie(vv_latency, pkt_size) = m_vv_perf_model->getAccessLatency(
        pkt_time, requester, address, access_type, perf);

    SubsecondTime processing_time = m_cxl_bandwidth.getRoundedLatency(pkt_size);

    // Compute Queue Delay
    SubsecondTime queue_delay;
    queue_delay = m_queue_model->computeQueueDelay(pkt_time + vv_latency, processing_time, requester);

    SubsecondTime access_latency = queue_delay + processing_time + vv_latency;

    switch(access_type){
        case CXLCntlrInterface::VN_READ:
            MYTRACE("VN_R ==%s== @ %016lx", itostr(pkt_time + queue_delay + processing_time).c_str(), address);
            perf->updateTime(pkt_time + access_latency, ShmemPerf::VN_DEVICE);
            break;
        case CXLCntlrInterface::VN_UPDATE:
            MYTRACE("VN_U ==%s== @ %016lx", itostr(pkt_time + queue_delay + processing_time).c_str(), address);
            break;
        default:
            LOG_PRINT_ERROR("Unrecognized CXL access Type: %u", access_type);
            break;
    }

    // Update Memory Counters
    m_num_accesses++;
    m_total_access_latency += access_latency;
    m_total_queueing_delay += queue_delay;

    return access_latency;
}

void CXLVNPerfModel::enable()
{
    m_enabled = true;
    m_vv_perf_model->enable();
}

void CXLVNPerfModel::disable() {
    m_enabled = false;
    m_vv_perf_model->disable();
}