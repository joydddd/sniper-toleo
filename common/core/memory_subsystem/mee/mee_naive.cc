#include "mee_naive.h"
#include "config.h"
#include "config.hpp"
#include "simulator.h"
#include "stats.h"

MEENaive::MEENaive(core_id_t core_id, UInt32 cache_block_size):
    MEEBase(core_id, cache_block_size)
{
    m_mme_perf_model = new MEEPerfModel(core_id);

    m_mac_cache_size = Sim()->getCfg()->getInt("perf_model/mee/mac_cache/size") * 1024; // KB -> Bytes
    UInt32 rounded_mac_size = (m_mac_length - 1) / 8 + 1;   // bits to bytes
    m_mac_per_cacheline = m_mac_cache_size / rounded_mac_size;

    m_mac_cache_tag_latency = SubsecondTime::NS(Sim()->getCfg()->getIntArray(
    "perf_model/mee/mac_cache/tags_access_time", core_id));
    m_mac_cache_data_latency = SubsecondTime::NS(Sim()->getCfg()->getIntArray(
    "perf_model/mee/mac_cache/data_access_time", core_id));

    registerStatsMetric("mee", core_id, "mac-gen-misses", &m_mac_gen_misses);
    registerStatsMetric("mee", core_id, "mac-verify-misses", &m_mac_verify_misses);
    registerStatsMetric("mee", core_id, "mac-dirty-evicts", &m_mac_dirty_evicts);

    m_mac_cache = new Cache(
        "mac-cache", "perf_model/mee/mac_cache", core_id, 
        1,                                // num sets
        m_mac_cache_size / getCacheBlockSize(),             // associativity
        getCacheBlockSize(),
        Sim()->getCfg()->getStringArray("perf_model/mee/mac_cache/replacement_policy", core_id),
        CacheBase::SHARED_CACHE,
        CacheBase::parseAddressHash(Sim()->getCfg()->getStringArray("perf_model/mee/mac_cache/address_hash", core_id)),
        NULL /* Fault Injection Manager */);
}

MEENaive::~MEENaive()
{
    delete m_mme_perf_model;
}

IntPtr MEENaive::getMACaddr(IntPtr address){
    return (address / (m_mac_per_cacheline * getCacheBlockSize())) * getCacheBlockSize();
}

IntPtr MEENaive::getOGaddr(IntPtr mac_addr){
    return mac_addr * m_mac_per_cacheline;
}

boost::tuple<SubsecondTime, HitWhere::where_t>
MEENaive::accessMAC(IntPtr address, Cache::access_t access, core_id_t requester, SubsecondTime now, ShmemPerf *perf){
   IntPtr mac_addr = getMACaddr(address);
   CacheBlockInfo* block_info = m_mac_cache->peekSingleLine(mac_addr);
   SubsecondTime latency = m_mac_cache_tag_latency;
   Byte data_buf[getCacheBlockSize()];
   bool cache_hit = false;
   if (block_info){
       cache_hit = true;
       m_mac_cache->accessSingleLine(mac_addr, access, data_buf, getCacheBlockSize(), now + latency, true);
       latency += m_mac_cache_data_latency;
       if (access == Cache::STORE)
         block_info->setCState(CacheState::MODIFIED);
   } 

   return boost::tuple<SubsecondTime, HitWhere::where_t>(latency, cache_hit ? HitWhere::MEE_CACHE : HitWhere::UNKNOWN);
}

boost::tuple<SubsecondTime, HitWhere::where_t>
MEENaive::genMAC(IntPtr address, core_id_t requester, SubsecondTime now){
    SubsecondTime crypto_latency = m_mme_perf_model->getcryptoLatency(now, requester, MEEBase::GEN_MAC, &m_dummy_shmem_perf);
    SubsecondTime access_latency;
    HitWhere::where_t hit_where;
    boost::tie(access_latency, hit_where) = accessMAC(address, Cache::STORE, requester, now + crypto_latency, &m_dummy_shmem_perf);
    m_mac_gen++;
    if (hit_where != HitWhere::MEE_CACHE)
        m_mac_gen_misses++;
    return boost::tuple<SubsecondTime, HitWhere::where_t>(crypto_latency + access_latency, hit_where);
}

boost::tuple<SubsecondTime, HitWhere::where_t>
MEENaive::verifyMAC(IntPtr address, core_id_t requester, SubsecondTime now, ShmemPerf *perf){
    SubsecondTime crypto_latency = m_mme_perf_model->getcryptoLatency(now, requester, MEEBase::GEN_MAC, perf);
    SubsecondTime access_latency;
    HitWhere::where_t hit_where;
    boost::tie(access_latency, hit_where) = accessMAC(address, Cache::LOAD, requester, now + crypto_latency, perf);
    m_mac_verify++;
    if (hit_where != HitWhere::MEE_CACHE)
        m_mac_verify_misses++;
    return boost::tuple<SubsecondTime, HitWhere::where_t>(crypto_latency > access_latency ? crypto_latency : access_latency, hit_where);
}

void MEENaive::insertMAC(IntPtr address, bool &dirty_eviction, IntPtr& _evict_addr, Cache::access_t access, core_id_t requester, SubsecondTime now){
    bool eviction;
    IntPtr evict_address;
    CacheBlockInfo evict_block_info;
    Byte evict_buf[m_cache_block_size];
    Byte data_buf[m_cache_block_size];
    IntPtr mac_addr = getMACaddr(address);
    dirty_eviction = false;

    m_mac_cache->insertSingleLine(mac_addr, data_buf, &eviction, &evict_address, &evict_block_info, evict_buf, now);
    m_mac_cache->peekSingleLine(mac_addr)->setCState(access == Cache::STORE ? CacheState::MODIFIED : CacheState::SHARED);

    // Writeback to DRAM done off-line, so don't affect return latency
    if (eviction && evict_block_info.getCState() == CacheState::MODIFIED)
    {
        dirty_eviction = true;
        _evict_addr = getOGaddr(evict_address);
        ++m_mac_dirty_evicts;
    }
}

SubsecondTime MEENaive::encryptData(IntPtr address, core_id_t requester, SubsecondTime now){
    SubsecondTime access_latency = m_mme_perf_model->getcryptoLatency(now, requester, MEEBase::ENCRYPT, &m_dummy_shmem_perf);
    m_num_encry++;
    return access_latency;
}

SubsecondTime MEENaive::decryptData(IntPtr address, core_id_t requester, SubsecondTime now, ShmemPerf *perf){
    SubsecondTime access_latency = m_mme_perf_model->getcryptoLatency(now, requester, MEEBase::DECRYPT, perf);
    m_num_decry++;
    return access_latency;
}


void MEENaive::enablePerfModel()
{
    m_mme_perf_model->enable();
}

void MEENaive::disablePerfModel()
{
    m_mme_perf_model->disable();
} 