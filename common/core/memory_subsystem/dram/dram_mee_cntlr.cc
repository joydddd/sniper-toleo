#include "dram_mee_cntlr.h"
#include "memory_manager.h"
#include "mee_naive.h"
#include "stats.h"

#if 0
#  define MYLOG_ENABLED
   extern Lock iolock;
#  include "core_manager.h"
#  include "simulator.h"
#  define MYLOG(...) {                                                                    \
   ScopedLock l(iolock);                                                                  \
   fflush(f_trace);                                                                        \
   fprintf(f_trace, "[%s] %d%cdr %-25s@%3u: ",                                                      \
   itostr(getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_SIM_THREAD)).c_str(),      \
         getMemoryManager()->getCore()->getId(),                                          \
         Sim()->getCoreManager()->amiUserThread() ? '^' : '_', __FUNCTION__, __LINE__);   \
   fprintf(f_trace, __VA_ARGS__); fprintf(f_trace, "\n"); fflush(f_trace); }
#else
#  define MYLOG(...) {}
#endif

DramMEECntlr::DramMEECntlr(MemoryManagerBase* memory_manager,
                  ShmemPerfModel* shmem_perf_model, UInt32 cache_block_size,
                  CXLAddressTranslator* cxl_address_translator,
                  DramCntlrInterface* dram_cntlr, core_id_t core_id)
    :DramCntlrInterface(memory_manager, shmem_perf_model, cache_block_size, cxl_address_translator)
    , m_dram_cntlr(dram_cntlr)
{
    m_mee = new MEENaive(memory_manager, shmem_perf_model, m_address_translator, core_id, m_cache_block_size, NULL, this);
    registerStatsMetric("dram", memory_manager->getCore()->getId(), "data-reads", &m_reads);
    registerStatsMetric("dram", memory_manager->getCore()->getId(), "data-writes", &m_writes);
    registerStatsMetric("dram", memory_manager->getCore()->getId(), "mac-reads", &m_mac_reads);
    registerStatsMetric("dram", memory_manager->getCore()->getId(), "mac-writes", &m_mac_writes);

#ifdef MYLOG_ENABLED
   std::ostringstream trace_filename;
   trace_filename << "dram_mee_cntlr_" << memory_manager->getCore()->getId()
                  << ".trace";
   f_trace = fopen(trace_filename.str().c_str(), "w+");
   std::cerr << "Create Dram mee cntlr trace " << trace_filename.str().c_str() << std::endl;
#endif // MYLOG_ENABLED
}

DramMEECntlr::~DramMEECntlr(){
    if (m_mee)
        delete m_mee;
}

SubsecondTime DramMEECntlr::getMAC(IntPtr mac_addr, core_id_t requester, Byte* buf, SubsecondTime now, ShmemPerf *perf){
    MYLOG("[%d]getMAC @ %016lx ", requester, mac_addr);
    m_mac_reads++;
    /* read MAC to dram */
    SubsecondTime dram_latency;
    HitWhere::where_t hit_where;
    boost::tie(dram_latency, hit_where) = m_dram_cntlr->getDataFromDram(mac_addr, requester, buf, now, perf, false); // mac_addr is physical address
    LOG_ASSERT_ERROR(hit_where == HitWhere::DRAM, "MAC %lu is not in local dram", mac_addr);
    return dram_latency;
}

SubsecondTime DramMEECntlr::putMAC(IntPtr mac_addr, core_id_t requester, Byte* buf, SubsecondTime now){
    MYLOG("[%d]putMAC @ %016lx ", requester, mac_addr);
    m_mac_writes++;
    /* write MAC to dram */
    SubsecondTime dram_latency;
    HitWhere::where_t hit_where;
    boost::tie(dram_latency, hit_where) = m_dram_cntlr->putDataToDram(mac_addr, requester, buf, now, false); // mac_addr is physical address
    LOG_ASSERT_ERROR(hit_where == HitWhere::DRAM, "MAC %lu is not in local dram", mac_addr);
    return SubsecondTime::Zero();
}


// address is virtual address. 
boost::tuple<SubsecondTime, HitWhere::where_t>
DramMEECntlr::getDataFromDram(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now, ShmemPerf *perf, bool is_virtual_addr){
    MYLOG("[%d]R @ %016lx ", requester, address);
    LOG_ASSERT_ERROR(is_virtual_addr, "DramMEECntlr::getDataFromDram: address is not virtual address"); 
    // DramMEECntlr only accepts virtual address
    SubsecondTime latency;

    /* get Data from local DRAM */
    SubsecondTime dram_latency;
    HitWhere::where_t hit_where;
    boost::tie(dram_latency, hit_where) = m_dram_cntlr->getDataFromDram(address, requester, data_buf, now, perf);
    LOG_ASSERT_ERROR(hit_where == HitWhere::DRAM, "Data %lu is not in local dram", address);

    /* get MAC and VN */
    SubsecondTime mac_latency, vn_latency;
    HitWhere::where_t vn_hit_where;
    boost::tie(mac_latency, vn_latency, vn_hit_where) = m_mee->fetchMACVN(address, requester, now, perf);
    latency = mac_latency > dram_latency ? mac_latency : dram_latency;
    if (vn_hit_where == HitWhere::CXL_VN){ // vn miss. vn request has already been sent to cxl
        hit_where = HitWhere::CXL_VN;
        latency = vn_latency;
    } else {
        /* if VN, MAC, salt and cipher are available */
        latency += m_mee->DecryptVerifyData(address, requester, now + latency, perf); // decrypt data and verify MAC
    }

    m_reads++;
    return boost::tuple<SubsecondTime, HitWhere::where_t>(latency, hit_where); // latency when MAC is ready
}

boost::tuple<SubsecondTime, HitWhere::where_t>
/* request updated VN, but not writing to dram yet */
DramMEECntlr::putDataToDram(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now, bool is_virtual_addr){
    return boost::tuple<SubsecondTime, HitWhere::where_t>(SubsecondTime::Zero(), HitWhere::CXL_VN);
}


SubsecondTime DramMEECntlr::handleVNUpdateFromCXL(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now){
    /* write latency is off critical path */
    SubsecondTime dram_latency, encrypt_latency;
    HitWhere::where_t hit_where;

    // generates cipher and MAC. MAC is written to mee cache. 
    encrypt_latency = m_mee->EncryptGenMAC(address, requester, now);

    // write cipher to DRAM
    boost::tie(dram_latency, hit_where) = m_dram_cntlr->putDataToDram(address, requester, data_buf, now + encrypt_latency);

    m_writes++;
    MYLOG("[%d]W_FINISH @ %016lx ", requester, address);
    return SubsecondTime::Zero();
}

SubsecondTime DramMEECntlr::handleVNverifyFromCXL(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now, ShmemPerf *perf){
    SubsecondTime mee_latency = m_mee->DecryptVerifyData(address, requester, now, perf);
    SubsecondTime data_verified_time = now + mee_latency > perf->getLastTime() ? now + mee_latency : perf->getLastTime();
    MYLOG("[%d]R_FINISH @ %016lx perf last time %ld, mee finish time %ld", requester, address, perf->getLastTime(), now + mee_latency);
    return data_verified_time - now;
}

void DramMEECntlr::enablePerfModel(){
    m_mee->enablePerfModel();
}

void DramMEECntlr::disablePerfModel(){
    m_mee->disablePerfModel();
}