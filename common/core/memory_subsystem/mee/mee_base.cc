#include "mee_base.h"
#include "stats.h"

// TODO: configurable page size
#define PAGE_OFFSET 12 // 4KB page


MEEBase::MEEBase(MemoryManagerBase* memory_manager, ShmemPerfModel* shmem_perf_model, core_id_t core_id, UInt32 cache_block_size):
    m_memory_manager(memory_manager),
    m_shmem_perf_model(shmem_perf_model),
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

IntPtr MEEBase::getMACaddr(IntPtr addr)
{
    UInt32 m_mac_per_cacheline = getCacheBlockSize() * 8 / getMACLength();
    IntPtr page_num = addr >> PAGE_OFFSET;
    IntPtr MACaddr = (addr & ((IntPtr)1 << PAGE_OFFSET - 1) / getCacheBlockSize()) / m_mac_per_cacheline;
    MACaddr = MACaddr * getCacheBlockSize() + page_num << PAGE_OFFSET;// round down to CacheBlock boundary
    return MACaddr;
}