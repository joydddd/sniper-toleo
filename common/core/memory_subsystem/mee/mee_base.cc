#include "mee_base.h"
#include "stats.h"

// TODO: configurable page size
#define PAGE_OFFSET 12 // 4KB page

MEEBase::MEEBase(MemoryManagerBase* memory_manager,
                 ShmemPerfModel* shmem_perf_model,
                 CXLAddressTranslator* address_translator, core_id_t core_id,
                 UInt32 cache_block_size)
    : m_memory_manager(memory_manager),
      m_shmem_perf_model(shmem_perf_model),
      m_address_translator(address_translator),
      m_encry_macs(0),
      m_decry_verifys(0),
      m_mac_reads(0),
      m_vn_length(Sim()->getCfg()->getInt("perf_model/mee/vn_length")), // in bits
      m_cache_block_size(cache_block_size),
      m_core_id(core_id) {

    registerStatsMetric("mee", core_id, "enc-mac", &m_encry_macs);
    registerStatsMetric("mee", core_id, "dec-verify" , &m_decry_verifys);
    registerStatsMetric("mee", core_id, "vnmac-fetch", &m_mac_reads);
}