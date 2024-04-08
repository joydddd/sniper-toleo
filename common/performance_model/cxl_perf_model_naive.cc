#include "cxl_perf_model_naive.h"
#include "simulator.h"
#include "config.h"
#include "config.hpp"
#include "stats.h"
#include "shmem_perf.h"
#include "dram_perf_model.h"

#if 0
#define MYTRACE_ENABLED
   extern Lock iolock;
#include "core_manager.h"
#include "simulator.h"
#define MYTRACE(...)                                    \
{                                                       \
    ScopedLock l(iolock);                               \
    fflush(f_trace);                                    \
    fprintf(f_trace, "[%d CXL] ", m_cxl_id);            \
    fprintf(f_trace, __VA_ARGS__);                      \
    fprintf(f_trace, "\n");                             \
    fflush(f_trace);                                    \
}
#else
#define MYTRACE(...) {}
#endif

CXLPerfModelNaive::CXLPerfModelNaive(cxl_id_t cxl_id, UInt64 transaction_size /* in bits */):
    CXLPerfModel(cxl_id, transaction_size),
    m_queue_model(NULL),
    m_cxl_bandwidth(8 * Sim()->getCfg()->getFloat("perf_model/cxl/memory_expander_" + itostr((unsigned int)cxl_id) + "/bandwidth")),
    /* cxl bandwidth converted to bit per ns. */
    m_total_queueing_delay(SubsecondTime::Zero()),
    m_total_channel_latency(SubsecondTime::Zero()),
    m_total_bytes_trans(0)
{
    m_cxl_access_cost =
        SubsecondTime::FS() *
        static_cast<uint64_t>(
            TimeConverter<float>::NStoFS(Sim()->getCfg()->getFloat(
                "perf_model/cxl/memory_expander_" + itostr((unsigned int)cxl_id) + "/latency"))); 
    m_queue_model = QueueModel::create(
        "cxl-queue", cxl_id,
        Sim()->getCfg()->getString("perf_model/cxl/queue_type"),
        m_cxl_bandwidth.getRoundedLatency(transaction_size));

    registerStatsMetric("cxl", cxl_id, "total-channel-latency", &m_total_channel_latency);
    registerStatsMetric("cxl", cxl_id, "total-queueing-delay", &m_total_queueing_delay);
    registerStatsMetric("cxl", cxl_id, "total-bytes-trans", &m_total_bytes_trans);

#ifdef MYTRACE_ENABLED
        std::ostringstream trace_filename;
    trace_filename << "cxl_perf_" << (int)m_cxl_id << ".trace";
    f_trace = fopen(trace_filename.str().c_str(), "w+");
    std::cerr << "Create CXL perf trace " << trace_filename.str().c_str() << std::endl;
#endif // MYTRACE_ENABLED
}

CXLPerfModelNaive::~CXLPerfModelNaive()
{
    if (m_queue_model)
    {
        delete m_queue_model;
        m_queue_model = NULL;
    }
}

SubsecondTime CXLPerfModelNaive::getAccessLatency(SubsecondTime pkt_time, UInt64 pkt_size, core_id_t requester, IntPtr address, CXLCntlrInterface::access_t access_type, ShmemPerf *perf)
{
    // pkt_size in bits
    // round up pkt_size to transaction size
    pkt_size = (pkt_size + m_transaction_size_bytes*8 - 1) / (m_transaction_size_bytes*8) * (m_transaction_size_bytes*8);

    if ((!m_enabled) || (requester >= (core_id_t) Config::getSingleton()->getApplicationCores()))
        return SubsecondTime::Zero();
    SubsecondTime processing_time = m_cxl_bandwidth.getRoundedLatency(pkt_size);

    // Compute Queue Delay
    SubsecondTime queue_delay;
    queue_delay = m_queue_model->computeQueueDelay(pkt_time, processing_time, requester);

    SubsecondTime cxl_latency = queue_delay + processing_time;

    cxl_latency += m_cxl_access_cost;

    switch(access_type){
        case CXLCntlrInterface::READ:
            MYTRACE("0x%016lx\tREAD\t%lu\t%lu\t%lu\t%lu\t%lu",address, pkt_time.getNS(), cxl_latency.getNS(), processing_time.getNS(), queue_delay.getNS(), m_cxl_access_cost.getNS());
            perf->updateTime(pkt_time);
            perf->updateTime(pkt_time + queue_delay, ShmemPerf::CXL_QUEUE);
            perf->updateTime(pkt_time + queue_delay + processing_time, ShmemPerf::CXL_BUS);
            perf->updateTime(pkt_time + cxl_latency, ShmemPerf::CXL_DEVICE);
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
    m_total_channel_latency += cxl_latency;
    m_total_queueing_delay += queue_delay;
    m_total_bytes_trans += pkt_size/8;

    return cxl_latency;
}