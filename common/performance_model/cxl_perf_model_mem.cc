#include "cxl_perf_model_mem.h"
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

CXLPerfModelMemoryExpander::CXLPerfModelMemoryExpander(cxl_id_t cxl_id, UInt64 transaction_size /* in bits */):
    CXLPerfModel(cxl_id, transaction_size),
    m_queue_model(NULL),
    m_cxl_bandwidth(8 * Sim()->getCfg()->getFloat("perf_model/cxl/memory_expander_" + itostr((unsigned int)cxl_id) + "/bandwidth")),
    m_total_queueing_delay(SubsecondTime::Zero()),
    m_total_access_latency(SubsecondTime::Zero()),
    m_dram_perf_model(NULL)
{
    m_dram_perf_model = DramPerfModel::createDramPerfModel(
            cxl_id, transaction_size/8, DramType::CXL_MEMORY);
    m_cxl_access_cost =
        SubsecondTime::FS() *
        static_cast<uint64_t>(
            TimeConverter<float>::NStoFS(Sim()->getCfg()->getFloat(
                "perf_model/cxl/memory_expander_" + itostr((unsigned int)cxl_id) + "/latency"))); 
    m_queue_model = QueueModel::create(
        "cxl-queue", cxl_id,
        Sim()->getCfg()->getString("perf_model/cxl/queue_type"),
        m_cxl_bandwidth.getRoundedLatency(transaction_size));

    registerStatsMetric("cxl", cxl_id, "total-access-latency", &m_total_access_latency);
    registerStatsMetric("cxl", cxl_id, "total-queueing-delay", &m_total_queueing_delay);

#ifdef MYTRACE_ENABLED
    std::ostringstream trace_filename;
    trace_filename << "cxl_perf_" << (int)m_cxl_id << ".trace";
    f_trace = fopen(trace_filename.str().c_str(), "w+");
    std::cerr << "Create CXL perf trace " << trace_filename.str().c_str() << std::endl;
#endif // MYTRACE_ENABLED
}

CXLPerfModelMemoryExpander::~CXLPerfModelMemoryExpander()
{
    if (m_queue_model)
    {
        delete m_queue_model;
        m_queue_model = NULL;
    }
    if (m_dram_perf_model){
        delete m_dram_perf_model;
        m_dram_perf_model = NULL;
    }
}

SubsecondTime CXLPerfModelMemoryExpander::getAccessLatency(SubsecondTime pkt_time, UInt64 pkt_size, core_id_t requester, IntPtr address, CXLCntlrInterface::access_t access_type, ShmemPerf *perf)
{
    if ((!m_enabled) || (requester >= (core_id_t) Config::getSingleton()->getApplicationCores()))
        return SubsecondTime::Zero();
    
    SubsecondTime processing_time = m_cxl_bandwidth.getRoundedLatency(pkt_size);

    // Compute Queue Delay
    SubsecondTime queue_delay;
    queue_delay = m_queue_model->computeQueueDelay(pkt_time, processing_time, requester);

    SubsecondTime access_latency = queue_delay + processing_time;

    SubsecondTime dram_latency = m_dram_perf_model->getAccessLatency(
        pkt_time + access_latency, pkt_size/8, requester, address,
        access_type == CXLCntlrInterface::access_t::READ
            ? DramCntlrInterface::access_t::READ
            : DramCntlrInterface::access_t::WRITE,
        perf);
    access_latency += dram_latency + m_cxl_access_cost;

    switch(access_type){
        case CXLCntlrInterface::READ:
            MYTRACE("0x%016lx\tREAD\t%lu\t%lu\t%lu\t%lu",address, pkt_time.getNS(), access_latency.getNS(), processing_time.getNS(), queue_delay.getNS());
            perf->updateTime(pkt_time);
            perf->updateTime(pkt_time + queue_delay, ShmemPerf::CXL_QUEUE);
            perf->updateTime(pkt_time + queue_delay + processing_time, ShmemPerf::CXL_BUS);
            perf->updateTime(pkt_time + access_latency, ShmemPerf::CXL_DEVICE);
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