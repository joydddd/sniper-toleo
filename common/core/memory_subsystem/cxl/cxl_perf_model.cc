#include "cxl_perf_model.h"
#include "simulator.h"
#include "config.h"
#include "config.hpp"
#include "stats.h"
#include "shmem_perf.h"

CXLPerfModel::CXLPerfModel(cxl_id_t cxl_id, ComponentBandwidth cxl_banchwidth, SubsecondTime cxl_access_cost, UInt64 cache_block_size /* in bits */):
    m_enabled(false),
    m_num_accesses(0),
    m_cxl_id(cxl_id),
    m_queue_model(NULL),
    m_cxl_access_cost(cxl_access_cost),
    m_cxl_bandwidth(cxl_banchwidth),
    m_total_queueing_delay(SubsecondTime::Zero()),
    m_total_access_latency(SubsecondTime::Zero())
{
    m_queue_model = QueueModel::create("cxl-queue", cxl_id, Sim()->getCfg()->getString("perf_model/cxl/queue_type"),
                                       m_cxl_bandwidth.getRoundedLatency(cache_block_size));
    
    registerStatsMetric("cxl", cxl_id, "total-access-latency", &m_total_access_latency);
    registerStatsMetric("cxl", cxl_id, "total-queueing-delay", &m_total_queueing_delay);
}

CXLPerfModel::~CXLPerfModel()
{
    if (m_queue_model)
    {
        delete m_queue_model;
        m_queue_model = NULL;
    }
}

SubsecondTime CXLPerfModel::getAccessLatency(SubsecondTime pkt_time, UInt64 pkt_size, core_id_t requester, IntPtr address, CXLCntlrInterface::access_t access_type, ShmemPerf *perf)
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
            perf->updateTime(pkt_time);
            perf->updateTime(pkt_time + queue_delay, ShmemPerf::CXL_QUEUE);
            perf->updateTime(pkt_time + queue_delay + processing_time, ShmemPerf::CXL_BUS);
            perf->updateTime(pkt_time + queue_delay + processing_time + m_cxl_access_cost, ShmemPerf::CXL_DEVICE);
            break;
        case CXLCntlrInterface::VN_READ:
            perf->updateTime(pkt_time + queue_delay + processing_time, ShmemPerf::VN);
            perf->updateTime(pkt_time + queue_delay + processing_time + m_cxl_access_cost, ShmemPerf::VN_DEVICE);
            break;
        case CXLCntlrInterface::VN_UPDATE:
        case CXLCntlrInterface::WRITE:
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