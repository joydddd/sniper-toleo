#include "cxl_vn_perf_model.h"
#include "simulator.h"
#include "config.h"
#include "config.hpp"
#include "stats.h"
#include "shmem_perf.h"

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

CXLVNPerfModel::CXLVNPerfModel(cxl_id_t cxl_id, ComponentBandwidth cxl_banchwidth, SubsecondTime cxl_access_cost, UInt64 cache_block_size /* in bits */):
    m_enabled(false),
    m_num_accesses(0),
    m_cxl_id(cxl_id),
    m_queue_model(NULL),
    m_cxl_access_cost(cxl_access_cost),
    m_cxl_bandwidth(cxl_banchwidth),
    m_total_queueing_delay(SubsecondTime::Zero()),
    m_total_access_latency(SubsecondTime::Zero())
{
    m_queue_model = QueueModel::create("vn-queue", cxl_id, Sim()->getCfg()->getString("perf_model/cxl/queue_type"),
                                       m_cxl_bandwidth.getRoundedLatency(cache_block_size));
    
    registerStatsMetric("vn-vault", cxl_id, "total-access-latency", &m_total_access_latency);
    registerStatsMetric("vn-vault", cxl_id, "total-queueing-delay", &m_total_queueing_delay);

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
}

SubsecondTime CXLVNPerfModel::getAccessLatency(SubsecondTime pkt_time, UInt64 pkt_size, core_id_t requester, IntPtr address, CXLCntlrInterface::access_t access_type, ShmemPerf *perf)
{
    // pkt_size is in 'bits'
    // m_dram_bandwidth is in 'Bits per clock cycle'
    if ((!m_enabled) || (requester >= (core_id_t) Config::getSingleton()->getApplicationCores()))
        return SubsecondTime::Zero();
    
    SubsecondTime processing_time = m_cxl_bandwidth.getRoundedLatency(pkt_size);

    // Compute Queue Delay
    SubsecondTime queue_delay;
    queue_delay = m_queue_model->computeQueueDelay(pkt_time, processing_time, requester);

    SubsecondTime access_latency = queue_delay + processing_time + m_cxl_access_cost;
    switch(access_type){
        case CXLCntlrInterface::READ:
            MYTRACE("R ==%s== @ %016lx", itostr(pkt_time + queue_delay + processing_time).c_str(), address);
            perf->updateTime(pkt_time);
            perf->updateTime(pkt_time + queue_delay, ShmemPerf::CXL_QUEUE);
            perf->updateTime(pkt_time + queue_delay + processing_time, ShmemPerf::CXL_BUS);
            perf->updateTime(pkt_time + queue_delay + processing_time + m_cxl_access_cost, ShmemPerf::CXL_DEVICE);
            break;
        case CXLCntlrInterface::VN_READ:
            MYTRACE("VN_R ==%s== @ %016lx", itostr(pkt_time + queue_delay + processing_time).c_str(), address);
            perf->updateTime(pkt_time + queue_delay + processing_time, ShmemPerf::VN);
            perf->updateTime(pkt_time + queue_delay + processing_time + m_cxl_access_cost, ShmemPerf::VN_DEVICE);
            break;
        case CXLCntlrInterface::VN_UPDATE:
            MYTRACE("VN_U ==%s== @ %016lx", itostr(pkt_time + queue_delay + processing_time).c_str(), address);
            break;
        case CXLCntlrInterface::WRITE:
            MYTRACE("W ==%s== @ %016lx", itostr(pkt_time + queue_delay + processing_time).c_str(), address);
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