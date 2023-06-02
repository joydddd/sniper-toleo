#include "mee_base.h"
#include "stats.h"

MEEBase::MEEBase(core_id_t core_id):
    m_num_encry(0), m_num_decry(0), m_mac_gen(0),
    m_core_id(core_id)
{
    m_vn_length = Sim()->getCfg()->getInt("perf_model/mee/vn_length");

    registerStatsMetric("mee", core_id, "encrypts", &m_num_encry);
    registerStatsMetric("mee", core_id, "decrypts" , &m_num_decry);
    registerStatsMetric("mee", core_id, "macs", &m_mac_gen);
}