#include "dram_mee_cntlr.h"
#include "memory_manager.h"
#include "mee_naive.h"

DramMEECntlr::DramMEECntlr(MemoryManagerBase* memory_manager,
                  ShmemPerfModel* shmem_perf_model, UInt32 cache_block_size,
                  CXLAddressTranslator* cxl_address_translator,
                  DramCntlrInterface* dram_cntlr, core_id_t core_id)
    :DramCntlrInterface(memory_manager, shmem_perf_model, cache_block_size, cxl_address_translator)
    , m_dram_cntlr(dram_cntlr)
{
    m_mee = new MEENaive(memory_manager, shmem_perf_model, core_id, m_cache_block_size, NULL);
}

DramMEECntlr::~DramMEECntlr(){
    if (m_mee)
        delete m_mee;
}

boost::tuple<SubsecondTime, HitWhere::where_t>
DramMEECntlr::getDataFromDram(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now, ShmemPerf *perf){
    SubsecondTime dram_latency, latency = SubsecondTime::Zero(); 
    HitWhere::where_t hit_where;
    boost::tie(dram_latency, hit_where) = m_dram_cntlr->getDataFromDram(address, requester, data_buf, now, perf);
    return  boost::tuple<SubsecondTime, HitWhere::where_t>(dram_latency, hit_where);
    // TODO: 
    // cxl_id_t cxl_id = m_address_translator->getHome(address);
    // if (cxl_id == HOST_CXL_ID) { // data is locates in local dram
    //     LOG_ASSERT_ERROR(hit_where == HitWhere::DRAM, "data is not in local dram");

    //     /* verify MAC */
    //     SubsecondTime mac_latency, a;
    //     HitWhere::where_t mac_hit_where;
    //     boost::tie(mac_latency, mac_hit_where) = m_mee->verifyMAC(address, requester, now, perf);
    //     if (mac_hit_where != HitWhere::MEE_CACHE){ // mac read cache miss
    //         boost::tie(mac_latency, mac_hit_where) = m_dram_cntlr->getDataFromDram(address, requester, data_buf, now + mac_latency, perf);
    //         LOG_ASSERT_ERROR(mac_hit_where == HitWhere::DRAM, "mac is not in local dram");
    //         IntPtr evict_addr;
    //         bool mac_writeback;
    //         m_mee->insertMAC(address, mac_writeback, evict_addr, Cache::LOAD, requester, now + mac_latency);
    //         /* writeback evicted MAC cachline to DRAM */
    //         if (mac_writeback)   boost::tie(a, mac_hit_where) = m_dram_cntlr->putDataToDram(evict_addr, requester, data_buf, now + mac_latency);
    //         LOG_ASSERT_ERROR(mac_hit_where == HitWhere::DRAM, "mac is not in local dram");
    //     }
    //     latency = mac_latency > dram_latency ? mac_latency : dram_latency;

    //     /* send msg to cxl vnserver cntlr*/
    //     getShmemPerfModel()->updateElapsedTime(now, ShmemPerfModel::_SIM_THREAD);
    //     getMemoryManager()->sendMsg(
    //     PrL1PrL2DramDirectoryMSI::ShmemMsg::CXL_VN_REQ, MemComponent::DRAM,
    //     MemComponent::CXL, requester, /* requester */
    //     0,                            /* receiver */
    //     address, NULL, 0, HitWhere::where_t::DRAM, perf,
    //     ShmemPerfModel::_SIM_THREAD);
    //     hit_where == HitWhere::CXL_VN;

    // }
    // return boost::tuple<SubsecondTime, HitWhere::where_t>(latency, hit_where);
}

boost::tuple<SubsecondTime, HitWhere::where_t>
/* request updated VN, but not writing to dram yet */
DramMEECntlr::putDataToDram(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now){
    m_dram_cntlr->putDataToDram(address, requester, data_buf, now);
    // TODO: 
    // cxl_id_t cxl_id = m_address_translator->getHome(address);
    // if (cxl_id == HOST_CXL_ID) { // data is written to local dram
    //     /* send msg to get updated vn */
    //     getShmemPerfModel()->updateElapsedTime(now, ShmemPerfModel::_SIM_THREAD);
    //     getMemoryManager()->sendMsg(
    //     PrL1PrL2DramDirectoryMSI::ShmemMsg::CXL_VN_UPDATE, MemComponent::DRAM,
    //     MemComponent::CXL, requester, /* requester */
    //     0,                            /* receiver */
    //     address, NULL, 0, HitWhere::UNKNOWN, &m_dummy_shmem_perf,
    //     ShmemPerfModel::_SIM_THREAD);
    //     return boost::tuple<SubsecondTime, HitWhere::where_t>(SubsecondTime::Zero(), HitWhere::CXL_VN);
    // }

    return boost::tuple<SubsecondTime, HitWhere::where_t>(SubsecondTime::Zero(), HitWhere::UNKNOWN);
}

SubsecondTime DramMEECntlr::handleVNUpdateFromCXL(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now){
    // TODO: 
    /* write latency is off critical path */
    /* generate MAC */
    // SubsecondTime mac_latency, dram_latency;
    // HitWhere::where_t mac_hit_where;
    // boost::tie(mac_latency, mac_hit_where) = m_mee->genMAC(address, requester, now);
    // if (mac_hit_where != HitWhere::MEE_CACHE){ // mac write cache miss
    //     boost::tie(mac_latency, mac_hit_where) = m_dram_cntlr->getDataFromDram(address, requester, data_buf, now + mac_latency, &m_dummy_shmem_perf);
    //     LOG_ASSERT_ERROR(mac_hit_where == HitWhere::DRAM, "mac is not in local dram");
    //     IntPtr evict_addr;
    //     bool mac_writeback;
    //     m_mee->insertMAC(address, mac_writeback, evict_addr, Cache::STORE, requester, now + mac_latency);
    //     /* write evicted MAC cachline to DRAM */
    //     if (mac_writeback)   boost::tie(dram_latency, mac_hit_where) = m_dram_cntlr->putDataToDram(evict_addr, requester, data_buf, now + mac_latency);
    //     LOG_ASSERT_ERROR(mac_hit_where == HitWhere::DRAM, "mac is not in local dram");
    // }

    // SubsecondTime encrypt_latency;
    // encrypt_latency = m_mee->encryptData(address, requester, now);
    // /* write data to DRAM*/
    // m_dram_cntlr->putDataToDram(address, requester, data_buf, now + encrypt_latency);
    return SubsecondTime::Zero();
}

SubsecondTime DramMEECntlr::handleVNverifyFromCXL(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now, ShmemPerf *perf){
    SubsecondTime decryption_latency = m_mee->decryptData(address, requester, now, perf);
    return decryption_latency;
}

void DramMEECntlr::enablePerfModel(){
    m_mee->enablePerfModel();
}

void DramMEECntlr::disablePerfModel(){
    m_mee->disablePerfModel();
}