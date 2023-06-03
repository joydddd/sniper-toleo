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
    m_mee = new MEENaive(core_id);
}

DramMEECntlr::~DramMEECntlr(){
    if (m_mee)
        delete m_mee;
}

boost::tuple<SubsecondTime, HitWhere::where_t>
DramMEECntlr::getDataFromDram(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now, ShmemPerf *perf){
    SubsecondTime dram_latency, decrypt_latency;; 
    HitWhere::where_t hit_where;
    boost::tie(dram_latency, hit_where) = m_dram_cntlr->getDataFromDram(address, requester, data_buf, now, perf);
    cxl_id_t cxl_id = m_address_translator->getHome(address);
    if (cxl_id == HOST_CXL_ID) { // data is locates in local dram
        /* decrypt memory fetch from local dram */
        decrypt_latency = m_mee->decryptData(address, requester, now + dram_latency, perf);

        /* send msg to cxl vnserver cntlr*/
        getShmemPerfModel()->updateElapsedTime(now, ShmemPerfModel::_SIM_THREAD);
        getMemoryManager()->sendMsg(
        PrL1PrL2DramDirectoryMSI::ShmemMsg::CXL_VN_REQ, MemComponent::DRAM,
        MemComponent::CXL, requester, /* requester */
        0,                            /* receiver */
        address, NULL, 0, HitWhere::where_t::DRAM, perf,
        ShmemPerfModel::_SIM_THREAD);
        return boost::tuple<SubsecondTime, HitWhere::where_t>(dram_latency + decrypt_latency, HitWhere::CXL_VN);
        // TODO: change to HitWhere::DRAM to enable speculation
    } else { // data is located on CXL
        return boost::tuple<SubsecondTime, HitWhere::where_t>(dram_latency, hit_where);
    }
}

boost::tuple<SubsecondTime, HitWhere::where_t>
/* request updated VN, but not writing to dram yet */
DramMEECntlr::putDataToDram(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now){
    cxl_id_t cxl_id = m_address_translator->getHome(address);
    if (cxl_id == HOST_CXL_ID) { // data is written to local dram 
        /* send msg to get updated vn */
        getShmemPerfModel()->updateElapsedTime(now, ShmemPerfModel::_SIM_THREAD);
        getMemoryManager()->sendMsg(
        PrL1PrL2DramDirectoryMSI::ShmemMsg::CXL_VN_UPDATE, MemComponent::DRAM,
        MemComponent::CXL, requester, /* requester */
        0,                            /* receiver */
        address, NULL, 0, HitWhere::UNKNOWN, &m_dummy_shmem_perf,
        ShmemPerfModel::_SIM_THREAD);
        return boost::tuple<SubsecondTime, HitWhere::where_t>(SubsecondTime::Zero(), HitWhere::CXL_VN);
    } else { // Data is written to CXL 
        return m_dram_cntlr->putDataToDram(address, requester, data_buf, now);
    }
    
}

SubsecondTime DramMEECntlr::handleVNUpdateFromCXL(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now){
    /* write latency is off critical path */
    SubsecondTime mac_latency, encrypt_latency;
    mac_latency = m_mee->genMAC(address, requester, now, &m_dummy_shmem_perf);
    encrypt_latency = m_mee->encryptData(address, requester, now + mac_latency);
    /* write to DRAM*/
    m_dram_cntlr->putDataToDram(address, requester, data_buf, now + mac_latency + encrypt_latency);
    return SubsecondTime::Zero();
}

SubsecondTime DramMEECntlr::handleVNverifyFromCXL(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now, ShmemPerf *perf){
    SubsecondTime mac_latency;
    mac_latency = m_mee->genMAC(address, requester, now, perf);
    return mac_latency;
}

void DramMEECntlr::enablePerfModel(){
    m_mee->enablePerfModel();
}

void DramMEECntlr::disablePerfModel(){
    m_mee->disablePerfModel();
}