#include "dram_mee_cntlr.h"
#include "memory_manager.h"
#include "mee_naive.h"
#include "stats.h"

#if 1
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
    m_mee = new MEENaive(memory_manager, shmem_perf_model, core_id, m_cache_block_size, NULL, this);
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

SubsecondTime DramMEECntlr::getVN(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now, ShmemPerf *perf){
    MYLOG("[%d]getVN @ %016lx ", requester, address);
    /* send msg to cxl vnserver cntlr to fetch VN */
    getShmemPerfModel()->updateElapsedTime(now, ShmemPerfModel::_SIM_THREAD);
    getMemoryManager()->sendMsg(
    PrL1PrL2DramDirectoryMSI::ShmemMsg::CXL_VN_REQ, MemComponent::DRAM,
    MemComponent::CXL, requester, /* requester */
    0,                            /* receiver */
    address, NULL, 0, HitWhere::where_t::DRAM, perf,
    ShmemPerfModel::_SIM_THREAD);
    return SubsecondTime::Zero();
}

SubsecondTime DramMEECntlr::updateVN(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now, ShmemPerf *perf){
    MYLOG("[%d]updateVN @ %016lx ", requester, address);
    /* send msg to get updated vn */
    getShmemPerfModel()->updateElapsedTime(now, ShmemPerfModel::_SIM_THREAD);
    getMemoryManager()->sendMsg(
    PrL1PrL2DramDirectoryMSI::ShmemMsg::CXL_VN_UPDATE, MemComponent::DRAM,
    MemComponent::CXL, requester, /* requester */
    0,                            /* receiver */
    address, NULL, 0, HitWhere::UNKNOWN, &m_dummy_shmem_perf,
    ShmemPerfModel::_SIM_THREAD);
    return SubsecondTime::Zero();
}


SubsecondTime DramMEECntlr::getMAC(IntPtr mac_addr, core_id_t requester, Byte* buf, SubsecondTime now, ShmemPerf *perf){
    MYLOG("[%d]getMAC @ %016lx ", requester, mac_addr);
    m_mac_reads++;
    /* read MAC to dram */
    SubsecondTime dram_latency;
    HitWhere::where_t hit_where;
    boost::tie(dram_latency, hit_where) = m_dram_cntlr->getDataFromDram(mac_addr, requester, buf, now, perf);
    LOG_ASSERT_ERROR(hit_where == HitWhere::DRAM, "MAC %lu is not in local dram", mac_addr);
    return dram_latency;
}

SubsecondTime DramMEECntlr::putMAC(IntPtr mac_addr, core_id_t requester, Byte* buf, SubsecondTime now){
    MYLOG("[%d]putMAC @ %016lx ", requester, mac_addr);
    m_mac_writes++;
    /* write MAC to dram */
    SubsecondTime dram_latency;
    HitWhere::where_t hit_where;
    boost::tie(dram_latency, hit_where) = m_dram_cntlr->putDataToDram(mac_addr, requester, buf, now);
    LOG_ASSERT_ERROR(hit_where == HitWhere::DRAM, "MAC %lu is not in local dram", mac_addr);
    return SubsecondTime::Zero();
}

boost::tuple<SubsecondTime, HitWhere::where_t>
DramMEECntlr::getDataFromDram(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now, ShmemPerf *perf){
    MYLOG("[%d]R @ %016lx ", requester, address);
    SubsecondTime dram_latency, decrypt_latency, latency = SubsecondTime::Zero(); 
    HitWhere::where_t hit_where;
    boost::tie(dram_latency, hit_where) = m_dram_cntlr->getDataFromDram(address, requester, data_buf, now, perf);
    // if data is not in local dram. request to fetch data has already been sent
    if (hit_where != HitWhere::DRAM){
        return boost::tuple<SubsecondTime, HitWhere::where_t>(dram_latency, hit_where);
    }
    // Data is located on local dram. Our MEE will handle it
    /* verify MAC */
    SubsecondTime mac_latency, vn_latency;
    HitWhere::where_t mac_hit_where;
    boost::tie(mac_latency, vn_latency, mac_hit_where) = m_mee->fetchMACVN(address, requester, now, perf);
    if (mac_hit_where == HitWhere::CXL_VN){ // mac read cache miss, VN is not cached. VN request already sent. 
        hit_where = HitWhere::CXL_VN;
    }
    latency = mac_latency > dram_latency ? mac_latency : dram_latency;
    m_reads++;
    return boost::tuple<SubsecondTime, HitWhere::where_t>(latency, hit_where); // latency when MAC is ready
}

boost::tuple<SubsecondTime, HitWhere::where_t>
/* request updated VN, but not writing to dram yet */
DramMEECntlr::putDataToDram(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now){
    MYLOG("[%d]W @ %016lx ", requester, address);
    SubsecondTime dram_latency;
    HitWhere::where_t hit_where;
    cxl_id_t cxl_id = m_address_translator->getHome(address);
    if (cxl_id != HOST_CXL_ID){// if data is not in local dram. send data to CXL
        m_dram_cntlr->putDataToDram(address, requester, data_buf, now);
        return boost::tuple<SubsecondTime, HitWhere::where_t>(dram_latency, hit_where);
    } 
    // data is located at local dram, our MEE will handle encryption & MAC
    updateVN(address, requester, data_buf, now, &m_dummy_shmem_perf); // fetch updated VN
    m_writes++;
    return boost::tuple<SubsecondTime, HitWhere::where_t>(SubsecondTime::Zero(), HitWhere::CXL_VN);
}

SubsecondTime DramMEECntlr::handleDataFromCXL(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now, ShmemPerf *perf) {
    MYLOG("[%d]CXL_LOAD_FINISH @ %016lx ", requester, address);
    m_dram_cntlr->handleDataFromCXL(address, requester, data_buf, now, perf);
    return SubsecondTime::Zero();
}

SubsecondTime DramMEECntlr::handleVNUpdateFromCXL(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now){
    /* write latency is off critical path */
    SubsecondTime mac_latency, dram_latency, encrypt_latency;
    HitWhere::where_t mac_hit_where, hit_where;
    encrypt_latency = m_mee->encryptData(address, requester, now); // gen cipher
    boost::tie(dram_latency, hit_where) = m_dram_cntlr->putDataToDram(address, requester, data_buf, now + encrypt_latency); // write cipher to DRAM
    boost::tie(mac_latency, mac_hit_where) = m_mee->genMAC(address, requester, now + encrypt_latency); // gen MAC
    
    MYLOG("[%d]W_FINISH @ %016lx ", requester, address);
    return SubsecondTime::Zero();
}

SubsecondTime DramMEECntlr::handleVNverifyFromCXL(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now, ShmemPerf *perf){
    SubsecondTime decryption_latency = m_mee->decryptData(address, requester, now, perf);
    SubsecondTime verify_latency = m_mee->verifyMAC(address, requester, now, perf);
    MYLOG("[%d]R_FINISH @ %016lx ", requester, address);
    return decryption_latency > verify_latency ? decryption_latency : verify_latency;
}

void DramMEECntlr::enablePerfModel(){
    m_mee->enablePerfModel();
}

void DramMEECntlr::disablePerfModel(){
    m_mee->disablePerfModel();
}