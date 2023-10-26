#include "mee_naive.h"
#include "config.h"
#include "config.hpp"
#include "simulator.h"
#include "stats.h"
#include "memory_manager_base.h"
#include "cxl_vnserver_cntlr.h"
#include "dram_mee_cntlr.h"

#if 0
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

MEENaive::MEENaive(MemoryManagerBase *memory_manager,
                   ShmemPerfModel *shmem_perf_model,
                   CXLAddressTranslator *address_translator, core_id_t core_id,
                   UInt32 cache_block_size, CXLCntlrInterface *cxl_cntlr,
                   DramCntlrInterface *dram_cntlr)
    : MEEBase(memory_manager, shmem_perf_model, address_translator, cxl_cntlr ? core_id + 1 : core_id,
              cache_block_size),
      m_cxl_id(cxl_cntlr ? core_id : HOST_CXL_ID),
      m_mac_misses(0), m_vn_misses(0),
      m_mee_perf_model(NULL),
      m_cxl_cntlr(cxl_cntlr),
      m_dram_cntlr(dram_cntlr),
      f_trace(NULL),
      m_enable_trace(false) {
    m_mee_perf_model = new MEEPerfModel(m_core_id);

    /* Allocate MAC cache */
    m_mac_cache_size = Sim()->getCfg()->getInt("perf_model/mee/cache/size") * 1024; // KB -> Bytes
    m_mac_per_cacheline = Sim()->getCfg()->getInt("perf_model/mee/mac_per_cl");

    m_mac_cache_tag_latency = SubsecondTime::NS(Sim()->getCfg()->getIntArray(
    "perf_model/mee/cache/tags_access_time", m_core_id));
    m_mac_cache_data_latency = SubsecondTime::NS(Sim()->getCfg()->getIntArray(
    "perf_model/mee/cache/data_access_time", m_core_id));

    registerStatsMetric("mee", m_core_id, "mac-misses", &m_mac_misses);
    registerStatsMetric("mee", m_core_id, "vn-misses", &m_vn_misses);

    m_mac_cache = new Cache(
        "mac-cache", "perf_model/mee/cache", m_core_id, 
        1,                                // num sets
        m_mac_cache_size / getCacheBlockSize(),             // associativity
        getCacheBlockSize(),
        Sim()->getCfg()->getStringArray("perf_model/mee/cache/replacement_policy", m_core_id),
        CacheBase::SHARED_CACHE,
        CacheBase::parseAddressHash(Sim()->getCfg()->getStringArray("perf_model/mee/cache/address_hash", m_core_id)),
        NULL /* Fault Injection Manager */);


    /* Allocate VN table */
    m_vn_table_entries = Sim()->getCfg()->getInt("perf_model/mee/vn_table/entries");
    m_vn_per_entry = Sim()->getCfg()->getInt("perf_model/mee/vn_table/vn_per_entry");
    m_vn_table_latency = SubsecondTime::NS(Sim()->getCfg()->getInt("perf_model/mee/vn_table/latency"));
    m_vn_table = new Cache("vn-cache", "perf_model/mee/vn-table", m_core_id,
                           m_vn_table_entries, 1, getCacheBlockSize() * m_vn_per_entry, "lru",
                           CacheBase::PR_L1_CACHE, CacheBase::HASH_MOD);

#ifdef MYLOG_ENABLED
    std::ostringstream trace_filename;
    trace_filename << "mee_cntlr_" << m_core_id << ".trace";
    f_trace = fopen(trace_filename.str().c_str(), "w+");
    std::cerr << "Create MEE cntlr trace " << trace_filename.str().c_str() << std::endl;
#endif // MYLOG_ENABLED
}

MEENaive::~MEENaive()
{
    delete m_mee_perf_model;
    delete m_vn_table;
    delete m_mac_cache;
}

boost::tuple<SubsecondTime, HitWhere::where_t>
MEENaive::accessMAC(IntPtr v_addr, Cache::access_t access, core_id_t requester, SubsecondTime now, ShmemPerf *perf){
    IntPtr mac_addr = m_address_translator->getMACAddrFromVirtual(v_addr);
    CacheBlockInfo *block_info = m_mac_cache->peekSingleLine(mac_addr);
    SubsecondTime latency = m_mac_cache_tag_latency;
    Byte data_buf[getCacheBlockSize()];
    HitWhere::where_t hit_where;
    if (block_info){ // MAC cache hit in MEE
        hit_where = HitWhere::MEE_CACHE;
        m_mac_cache->accessSingleLine(mac_addr, access, data_buf, getCacheBlockSize(), now + latency, true);
        latency += m_mac_cache_data_latency;
        if (access == Cache::STORE) block_info->setCState(CacheState::MODIFIED);
    } else {// MAC cache miss in MEE
        // Always get mac from DRAM/CXL cntlr (WRITE ALLOCATE)
        hit_where = m_cxl_cntlr ? HitWhere::CXL : HitWhere::DRAM;
        MYLOG("[%d]load MAC @ %016lx", requester, mac_addr);
        if (m_cxl_cntlr) { // of native type CXLVNServerCntlr
            latency += ((CXLVNServerCntlr*) m_cxl_cntlr)->getMAC(mac_addr, requester, m_cxl_id, data_buf, now + latency, perf);
        } else if (m_dram_cntlr) {
            latency += ((DramMEECntlr*) m_dram_cntlr)->getMAC(mac_addr, requester, data_buf, now + latency, perf);
        } else {
            LOG_ASSERT_ERROR(true, "No DRAM/CXL cntlr presents on MEE cntlr %d", m_core_id);
        }
        insertMAC(access, mac_addr, requester, now + latency);
        m_mac_misses++;
    }

   return boost::tuple<SubsecondTime, HitWhere::where_t>(latency, hit_where);
}


void MEENaive::insertMAC(Cache::access_t access, IntPtr mac_addr, core_id_t requester, SubsecondTime now){
    bool eviction;
    IntPtr evict_address;
    CacheBlockInfo evict_block_info;
    Byte evict_buf[m_cache_block_size];
    Byte data_buf[m_cache_block_size];

    m_mac_cache->insertSingleLine(mac_addr, data_buf, &eviction, &evict_address, &evict_block_info, evict_buf, now);
    m_mac_cache->peekSingleLine(mac_addr)->setCState(access == Cache::STORE ? CacheState::MODIFIED : CacheState::SHARED);

    // Writeback to DRAM/CXL done off-line, so don't affect return latency
    if (eviction && evict_block_info.getCState() == CacheState::MODIFIED){
        MYLOG("[%d]evict MAC @ %016lx", requester, evict_address);
        if (m_cxl_cntlr) ((CXLVNServerCntlr*) m_cxl_cntlr)->putMAC(evict_address, requester, m_cxl_id, data_buf, now);
        else if (m_dram_cntlr) ((DramMEECntlr*) m_dram_cntlr)->putMAC(evict_address, requester, data_buf, now);
        else LOG_ASSERT_ERROR(true, "No DRAM/CXL cntlr presents on MEE cntlr %d", m_core_id);
    }
}

/* Call on DecryptVerifyData. Insert VN entry if entry does not exist */
bool MEENaive::insertVN(IntPtr v_addr, core_id_t requester, SubsecondTime now){
    IntPtr vn_entry_idx = v_addr / (m_vn_per_entry * getCacheBlockSize());
    bool hit = m_vn_table->accessSingleLine(vn_entry_idx, Cache::LOAD, NULL, 0, now, false);
    if (hit) return false;

    /* Allocate VN entry */
    bool eviction; 
    IntPtr evict_address;
    CacheBlockInfo evict_block_info;
    m_vn_table->insertSingleLine(vn_entry_idx, NULL, &eviction, &evict_address,
                                &evict_block_info, NULL, now);
    // Always quiet eviction (no writeback)
    return true;
}

/* lookup VN table. return HitWhere::CXL_VN or HitWhere::MEE_CACHE, access latency
* In case of cache miss: 
*     for CXL MEEs, get VN locally on this CXL. 
*     for DRAM MEEs, do nothing
* Neither allocates the entry for the VN fetched / to be fetched. 
*/
boost::tuple<SubsecondTime, HitWhere::where_t> 
MEENaive::lookupVN(IntPtr v_addr, core_id_t requester, SubsecondTime now, ShmemPerf* perf){
    SubsecondTime latency = m_vn_table_latency;
    IntPtr vn_entry_idx = v_addr / (m_vn_per_entry * getCacheBlockSize());
    bool hit = m_vn_table->accessSingleLine(vn_entry_idx, Cache::LOAD, NULL, 0, now, true);
    if (hit) {
        return boost::make_tuple<SubsecondTime, HitWhere::where_t>(latency, HitWhere::MEE_CACHE);
    } 


    /* If we're on CXL, fetch VN from CXL */
    if (m_cxl_cntlr){
        MYLOG("[%d]fetch VN @ %016lx", requester, vn_entry_idx);
        latency += ((CXLVNServerCntlr*) m_cxl_cntlr)->getVN(v_addr, requester, now, perf);
        // do not allocate vn entry. Only allocate on DecryptVerifyData. 
    }
    m_vn_misses++;
    return boost::make_tuple<SubsecondTime, HitWhere::where_t>(latency, HitWhere::CXL_VN);
}


SubsecondTime MEENaive::EncryptGenMAC(IntPtr address, core_id_t requester, SubsecondTime now) {
    HitWhere::where_t hit_where;
    SubsecondTime mac_latency; 
    boost::tie(mac_latency, hit_where) = accessMAC(address, Cache::STORE, requester, now, &m_dummy_shmem_perf);
    /* WRITE ALLOCATED cache also fetches salt for us. */

    SubsecondTime aes_latency = m_mee_perf_model->getAESLatency(now + mac_latency, requester, MEEBase::ENCRYPT, &m_dummy_shmem_perf);

    m_encry_macs++;
    return aes_latency + mac_latency;
}

SubsecondTime MEENaive::DecryptVerifyData(IntPtr address, core_id_t requester, SubsecondTime now, ShmemPerf *perf) {
    /* salt, VN & MAC have already been fetched */

    // insert VN to cache if neccessary. This operation is off the critical path
    insertVN(address, requester, now);

    SubsecondTime aes_latency = m_mee_perf_model->getAESLatency(now, requester, MEEBase::DECRYPT, perf);

    m_decry_verifys++;
    return aes_latency;
}

/* during READ: fetch MAC and VN from corresponding mem ctnlr/mee cache
Return: MAC latency, VN latency, hitwhere */
boost::tuple<SubsecondTime, SubsecondTime, HitWhere::where_t>
MEENaive::fetchMACVN(IntPtr address, core_id_t requester, SubsecondTime now,
                     ShmemPerf *perf) {
    SubsecondTime mac_latency, vn_latency;
    HitWhere::where_t mac_hit_where, vn_hit_where;
    
    boost::tie(mac_latency, mac_hit_where) = accessMAC(address, Cache::LOAD, requester, now, perf);
    boost::tie(vn_latency, vn_hit_where) = lookupVN(address, requester, now, perf);

    m_mac_reads++;

    return boost::make_tuple<SubsecondTime, SubsecondTime, HitWhere::where_t>(
        mac_latency, vn_latency,
        vn_hit_where == HitWhere::CXL_VN ? HitWhere::CXL_VN : HitWhere::DRAM);
}


void MEENaive::enablePerfModel()
{
    m_mee_perf_model->enable();
    m_enable_trace = true;
}

void MEENaive::disablePerfModel()
{
    m_mee_perf_model->disable();
    m_enable_trace = false;
}