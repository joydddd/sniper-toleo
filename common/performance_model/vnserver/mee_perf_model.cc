#include "mee_perf_model.h"
#include "simulator.h"
#include "config.h"
#include "config.hpp"
#include "stats.h"

MEEPerfModel::MEEPerfModel(core_id_t mee_id):
    m_enabled(false),
    m_num_accesses(0),
    m_mee_id(mee_id),
    m_aes_queue_model(NULL),
    m_mee_freq(Sim()->getCfg()->getFloat("perf_model/mee/frequency")*1000000),
    m_mee_period(ComponentPeriod::fromFreqHz(m_mee_freq)),
    m_aes_latency(SubsecondTimeCycleConverter(&m_mee_period).cyclesToSubsecondTime(Sim()->getCfg()->getInt("perf_model/mee/latency"))),
    m_aes_bandwidth(Sim()->getCfg()->getInt("perf_model/mee/bandwidth") * m_mee_freq), // num of aes encryption per cycle
    m_aes_per_req(Sim()->getCfg()->getInt("perf_model/mee/aes_per_access")),
    m_total_queueing_delay(SubsecondTime::Zero()),
    m_total_aes_latency(SubsecondTime::Zero())
{
    m_aes_queue_model = QueueModel::create(
        "mee-queue", m_mee_id,
        Sim()->getCfg()->getString("perf_model/mee/queue_type"),
        m_aes_bandwidth.getRoundedLatency(1));

    registerStatsMetric("mee", m_mee_id, "total-aes-latency", &m_total_aes_latency);
    registerStatsMetric("mee", m_mee_id, "total-queueing-delay", &m_total_queueing_delay);

}

MEEPerfModel::~MEEPerfModel()
{
    delete m_aes_queue_model;
}

SubsecondTime MEEPerfModel::getAESLatency(SubsecondTime now, core_id_t requester, MEEBase::MEE_op_t op_type, ShmemPerf *perf){
    if ((!m_enabled) || (requester >= (core_id_t) Config::getSingleton()->getApplicationCores()))
        return SubsecondTime::Zero();
    
    // each encrypt + mac / decrypt + mac operation contains 5 aes operations 
    SubsecondTime processing_time = m_aes_bandwidth.getRoundedLatency(m_aes_per_req); 

    // Compute Queue Delay
    SubsecondTime queue_delay;
    queue_delay = m_aes_queue_model->computeQueueDelay(now, processing_time, requester);

    SubsecondTime access_latency = queue_delay + m_aes_latency;
    
    perf->updateTime(now);
    perf->updateTime(now + access_latency, ShmemPerf::MEE);

    // Update Memory Counters
    m_num_accesses++;
    m_total_aes_latency += access_latency;
    m_total_queueing_delay += queue_delay;

    return  access_latency;
}