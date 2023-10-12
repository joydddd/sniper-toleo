#include "mee_perf_model.h"
#include "simulator.h"
#include "config.h"
#include "config.hpp"
#include "stats.h"

MEEPerfModel::MEEPerfModel(core_id_t mee_id):
    m_enabled(false),
    m_num_accesses(0),
    m_mee_id(mee_id),
    m_queue_model(NULL),
    m_mee_freq(Sim()->getCfg()->getFloat("perf_model/mee/frequency")*1000000),
    m_mee_period(ComponentPeriod::fromFreqHz(m_mee_freq)),
    m_aes_latency(SubsecondTimeCycleConverter(&m_mee_period).cyclesToSubsecondTime(Sim()->getCfg()->getInt("perf_model/mee/latency"))),
    m_aes_bandwidth(Sim()->getCfg()->getInt("perf_model/mee/bandwidth") * m_mee_freq), // num of cipher per ns
    m_total_queueing_delay(SubsecondTime::Zero()),
    m_total_crypto_latency(SubsecondTime::Zero()),
    m_total_mac_latency(SubsecondTime::Zero())
{
    m_queue_model = QueueModel::create("mee-queue", m_mee_id, Sim()->getCfg()->getString("perf_model/mee/queue_type"), m_aes_bandwidth.getRoundedLatency(1));

    registerStatsMetric("mee", m_mee_id, "total-crypto-latency", &m_total_crypto_latency);
    registerStatsMetric("mee", m_mee_id, "total-mac-latency", &m_total_mac_latency);
    registerStatsMetric("mee", m_mee_id, "total-queueing-delay", &m_total_queueing_delay);

}

MEEPerfModel::~MEEPerfModel()
{
    delete m_queue_model;
}

SubsecondTime MEEPerfModel::getcryptoLatency(SubsecondTime now, core_id_t requester, MEEBase::MEE_op_t op_type, ShmemPerf *perf){
    if ((!m_enabled) || (requester >= (core_id_t) Config::getSingleton()->getApplicationCores()))
        return SubsecondTime::Zero();
    
    SubsecondTime processing_time = m_aes_bandwidth.getRoundedLatency(1);

    // Compute Queue Delay
    SubsecondTime queue_delay;
    queue_delay = m_queue_model->computeQueueDelay(now, processing_time, requester);

    SubsecondTime access_latency = queue_delay + m_aes_latency;
    
    perf->updateTime(now);
    perf->updateTime(now + access_latency, ShmemPerf::MEE);

    // Update Memory Counters
    m_num_accesses++;
    switch(op_type){
        case MEEBase::GEN_MAC:
            m_total_mac_latency += access_latency;
            break;
        case MEEBase::DECRYPT:
        case MEEBase::ENCRYPT:
            m_total_crypto_latency += access_latency;
            break;
        default:
            LOG_ASSERT_ERROR(false, "Unknown MEE operation type %d", op_type);
    }
    m_total_queueing_delay += queue_delay;

    return  access_latency;
}