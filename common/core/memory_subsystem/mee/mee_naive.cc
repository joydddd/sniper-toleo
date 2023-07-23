#include "mee_naive.h"
#include "config.h"
#include "config.hpp"
#include "simulator.h"
#include "stats.h"
#include "memory_manager_base.h"
#include "cxl_vnserver_cntlr.h"
#include "dram_mee_cntlr.h"

#if 1
#define MYLOG_ENABLED
 extern Lock iolock;
#include "core_manager.h"
#include "simulator.h"
# define MYLOG(...) if (m_enable_trace){                                                   \
    ScopedLock l(iolock);                                                                  \
    fflush(f_trace);                                                                       \
    fprintf(f_trace, "[%s] %d%cdr %-25s@%3u: ",                                            \
    itostr(getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_SIM_THREAD)).c_str(),      \
         getMemoryManager()->getCore()->getId(),                                          \
         Sim()->getCoreManager()->amiUserThread() ? '^' : '_', __FUNCTION__, __LINE__);   \
    fprintf(f_trace, __VA_ARGS__); fprintf(f_trace, "\n"); fflush(f_trace); }
#else
#define MYLOG(...) {}
#endif


MEENaive::MEENaive(MemoryManagerBase* memory_manager, ShmemPerfModel* shmem_perf_model, core_id_t core_id, UInt32 cache_block_size, CXLCntlrInterface* cxl_cntlr, DramCntlrInterface *dram_cntlr):
    MEEBase(memory_manager, shmem_perf_model, core_id, cache_block_size)
    , m_cxl_cntlr(cxl_cntlr)
    , m_dram_cntlr(dram_cntlr)
    , f_trace(NULL)
    , m_enable_trace(false)
{
    m_mme_perf_model = new MEEPerfModel(core_id);

    m_mee_cache_size = Sim()->getCfg()->getInt("perf_model/mee/cache/size") * 1024; // KB -> Bytes
    UInt32 rounded_mac_size = (m_mac_length - 1) / 8 + 1;   // bits to bytes
    m_mac_per_cacheline = m_mee_cache_size / rounded_mac_size;

    m_mee_cache_tag_latency = SubsecondTime::NS(Sim()->getCfg()->getIntArray(
    "perf_model/mee/cache/tags_access_time", core_id));
    m_mee_cache_data_latency = SubsecondTime::NS(Sim()->getCfg()->getIntArray(
    "perf_model/mee/cache/data_access_time", core_id));

    registerStatsMetric("mee", core_id, "mac-gen-misses", &m_mac_gen_misses);
    registerStatsMetric("mee", core_id, "mac-fetch-misses", &m_mac_fetch_misses);

    m_mee_cache = new Cache(
        "mac-cache", "perf_model/mee/cache", core_id, 
        1,                                // num sets
        m_mee_cache_size / getCacheBlockSize(),             // associativity
        getCacheBlockSize(),
        Sim()->getCfg()->getStringArray("perf_model/mee/cache/replacement_policy", core_id),
        CacheBase::SHARED_CACHE,
        CacheBase::parseAddressHash(Sim()->getCfg()->getStringArray("perf_model/mee/cache/address_hash", core_id)),
        NULL /* Fault Injection Manager */);
#ifdef MYLOG_ENABLED
    std::ostringstream trace_filename;
    trace_filename << "mee_cntlr_" << core_id << ".trace";
    f_trace = fopen(trace_filename.str().c_str(), "w+");
    std::cerr << "Create MEE cntlr trace " << trace_filename.str().c_str() << std::endl;
#endif // MYLOG_ENABLED
}

MEENaive::~MEENaive()
{
    delete m_mme_perf_model;
}

boost::tuple<SubsecondTime, SubsecondTime, HitWhere::where_t> 
MEENaive::accessMACVN(IntPtr address, Cache::access_t access, core_id_t requester, SubsecondTime now, ShmemPerf *perf){
    IntPtr mac_addr = CXLAddressTranslator::getMACaddr(address);
    CacheBlockInfo *block_info = m_mee_cache->peekSingleLine(mac_addr);
    SubsecondTime latency = m_mee_cache_tag_latency, mac_latency = SubsecondTime::Zero(), vn_latency = SubsecondTime::Zero();
    Byte data_buf[getCacheBlockSize()];
    HitWhere::where_t hit_where;
    if (block_info){ // MAC cache hit in MEE
        hit_where = HitWhere::MEE_CACHE;
        m_mee_cache->accessSingleLine(mac_addr, access, data_buf, getCacheBlockSize(), now + latency, true);
        latency += m_mee_cache_data_latency;
        mac_latency = latency;
        vn_latency = latency;
        if (access == Cache::STORE) block_info->setCState(CacheState::MODIFIED);
    } else {// MAC cache miss in MEE
        if (access == Cache::LOAD)
        {   // for LOADs, get mac from DRAM/CXL cntlr, vn from VN Vault
            hit_where = HitWhere::CXL_VN;
            MYLOG("[%d]load MAC @ %016lx", requester, mac_addr);
            if (m_cxl_cntlr) { // of native type CXLVNServerCntlr
                mac_latency = ((CXLVNServerCntlr*) m_cxl_cntlr)->getMAC(mac_addr, requester, data_buf, now + latency, perf);
                vn_latency = ((CXLVNServerCntlr*) m_cxl_cntlr)->getVN(address, requester, data_buf, now + latency, perf);
            } else if (m_dram_cntlr) {
                mac_latency = ((DramMEECntlr*) m_dram_cntlr)->getMAC(mac_addr, requester, data_buf, now + latency, perf);
                ((DramMEECntlr*) m_dram_cntlr)->getVN(address, requester, data_buf, now + latency, perf); // fetches VN from CXL VN Vault
            } else {
                LOG_ASSERT_ERROR(true, "No DRAM/CXL cntlr presents on MEE cntlr %d", m_core_id);
            }
        }
        mac_latency += latency;
        vn_latency += latency;
        insertMAC(access, mac_addr, requester, now + mac_latency);
    }

   return boost::tuple<SubsecondTime, SubsecondTime, HitWhere::where_t>(mac_latency, vn_latency, hit_where);
}

void MEENaive::insertMAC(Cache::access_t access, IntPtr mac_addr, core_id_t requester, SubsecondTime now){
    bool eviction;
    IntPtr evict_address;
    CacheBlockInfo evict_block_info;
    Byte evict_buf[m_cache_block_size];
    Byte data_buf[m_cache_block_size];

    m_mee_cache->insertSingleLine(mac_addr, data_buf, &eviction, &evict_address, &evict_block_info, evict_buf, now);
    m_mee_cache->peekSingleLine(mac_addr)->setCState(access == Cache::STORE ? CacheState::MODIFIED : CacheState::SHARED);

    // Writeback to DRAM/CXL done off-line, so don't affect return latency
    if (eviction && evict_block_info.getCState() == CacheState::MODIFIED){
        MYLOG("[%d]evict MAC @ %016lx", requester, evict_address);
        if (m_cxl_cntlr) ((CXLVNServerCntlr*) m_cxl_cntlr)->putMAC(evict_address, requester, data_buf, now);
        else if (m_dram_cntlr) ((DramMEECntlr*) m_dram_cntlr)->putMAC(evict_address, requester, data_buf, now);
        else LOG_ASSERT_ERROR(true, "No DRAM/CXL cntlr presents on MEE cntlr %d", m_core_id);
    }
}

boost::tuple<SubsecondTime, HitWhere::where_t>
MEENaive::genMAC(IntPtr address, core_id_t requester, SubsecondTime now){
    SubsecondTime crypto_latency = m_mme_perf_model->getcryptoLatency(now, requester, MEEBase::GEN_MAC, &m_dummy_shmem_perf);
    SubsecondTime mac_latency, vn_latency;
    HitWhere::where_t hit_where;
    // write MAC & VN to MEE cache
    boost::tie(mac_latency, vn_latency, hit_where) = accessMACVN(address, Cache::STORE, requester, now + crypto_latency, &m_dummy_shmem_perf);
    m_mac_gen++;
    if (hit_where != HitWhere::MEE_CACHE)
        m_mac_gen_misses++;
    MYLOG("[%d]genMAC @ %016lx %s", requester, address, hit_where == HitWhere::MEE_CACHE ? "hit" : "miss")
    return boost::tuple<SubsecondTime, HitWhere::where_t>(crypto_latency, hit_where);
}

SubsecondTime
MEENaive::verifyMAC(IntPtr address, core_id_t requester, SubsecondTime now, ShmemPerf *perf){
    MYLOG("[%d]verifyMAC @ %016lx", requester, address)
    m_mac_verify++;
    return m_mme_perf_model->getcryptoLatency(now, requester, MEEBase::GEN_MAC, perf);
}

boost::tuple<SubsecondTime, SubsecondTime, HitWhere::where_t>
MEENaive::fetchMACVN(IntPtr address, core_id_t requester, SubsecondTime now, ShmemPerf *perf){
    SubsecondTime mac_latency, vn_latency; 
    HitWhere::where_t hit_where;
    boost::tie(mac_latency, vn_latency, hit_where) = accessMACVN(address, Cache::LOAD, requester, now, perf);
    m_mac_fetch++;
    if (hit_where != HitWhere::MEE_CACHE)
        m_mac_fetch_misses++;
    MYLOG("[%d]fetchMACVN @ %016lx %s", requester, address, hit_where == HitWhere::MEE_CACHE ? "hit" : "miss")
    return boost::tuple<SubsecondTime, SubsecondTime, HitWhere::where_t>(mac_latency, vn_latency, hit_where);
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
    m_enable_trace = true;
}

void MEENaive::disablePerfModel()
{
    m_mme_perf_model->disable();
    m_enable_trace = false;
} 