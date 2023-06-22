#include "mee_base.h"
#include "stats.h"

MEEBase::MEEBase(core_id_t core_id, UInt32 cache_block_size):
    m_num_encry(0), m_num_decry(0), 
    m_mac_gen(0), m_mac_verify(0),
    m_cache_block_size(cache_block_size),
    m_core_id(core_id)
{
    m_vn_length = Sim()->getCfg()->getInt("perf_model/mee/vn_length"); // in bits
    m_mac_length = Sim()->getCfg()->getInt("perf_model/mee/mac_length"); // in bits

    registerStatsMetric("mee", core_id, "encrypts", &m_num_encry);
    registerStatsMetric("mee", core_id, "decrypts" , &m_num_decry);
    registerStatsMetric("mee", core_id, "mac-gen", &m_mac_gen);
    registerStatsMetric("mee", core_id, "mac-verify", &m_mac_verify);
}