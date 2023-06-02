#include "cxl_vnserver_cntlr.h"
#include "stats.h"
#include "config.h"
#include "config.hpp"

#if 1
#  define MYLOG_ENABLED
   extern Lock iolock;
#  include "core_manager.h"
#  include "simulator.h"
#  define MYLOG(...) if (m_enable_trace){                                                   \
   ScopedLock l(iolock);                                                                  \
   fflush(f_trace);                                                                       \
   fprintf(f_trace, "[%s] %d%cdr %-25s@%3u: ",                                            \
   itostr(getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_SIM_THREAD)).c_str(),      \
         getMemoryManager()->getCore()->getId(),                                          \
         Sim()->getCoreManager()->amiUserThread() ? '^' : '_', __FUNCTION__, __LINE__);   \
   fprintf(f_trace, __VA_ARGS__); fprintf(f_trace, "\n"); fflush(f_trace); }
#else
#  define MYLOG(...) {}
#endif

CXLVNServerCntlr::CXLVNServerCntlr(
      MemoryManagerBase* memory_manager, ShmemPerfModel* shmem_perf_model, UInt32 cache_block_size,
      CXLAddressTranslator* cxl_address_tranlator, CXLCntlr *cxl_cntlr, MEEBase* mee):
    CXLCntlrInterface(memory_manager, shmem_perf_model, cache_block_size, cxl_address_tranlator),
    m_vnserver_cxl_id(cxl_address_tranlator->getnumCXLDevices()),
    m_vn_length(mee->getVNLength()), // vn_length in bits 
    m_vn_reads(0),
    m_vn_updates(0),
    m_cxl_cntlr(cxl_cntlr),
    m_mee(mee),
    f_trace(NULL),
    m_enable_trace(false)
{
   m_cxl_pkt_size = Sim()->getCfg()->getInt("perf_model/cxl/vnserver/pkt_size");
   m_vn_length = Sim()->getCfg()->getInt("perf_model/mee/vn_length");

   SubsecondTime vnserver_access_cost =
       SubsecondTime::FS() *
       static_cast<uint64_t>(TimeConverter<float>::NStoFS(
           Sim()->getCfg()->getFloat("perf_model/cxl/vnserver/latency"))); 
   ComponentBandwidth  vnserver_bandwidth =  8 * Sim()->getCfg()->getFloat("perf_model/cxl/vnserver/bandwidth");  
      // Convert bytes to bits
   m_vn_perf_model = new CXLPerfModel(m_vnserver_cxl_id, vnserver_bandwidth, vnserver_access_cost, m_cxl_pkt_size);
   registerStatsMetric("cxl", m_vnserver_cxl_id, "reads", &m_vn_reads);
   registerStatsMetric("cxl", m_vnserver_cxl_id, "writes", &m_vn_updates);


#ifdef MYLOG_ENABLED
   std::ostringstream trace_filename;
   trace_filename << "cxl_vnserver_cntlr" << ".trace";
   f_trace = fopen(trace_filename.str().c_str(), "w+");
   std::cerr << "Create CXL vnserver cntlr trace " << trace_filename.str().c_str() << std::endl;
#endif // MYLOG_ENABLED
}


CXLVNServerCntlr::~CXLVNServerCntlr()
{
#ifdef MYLOG_ENABLED
   fclose(f_trace);
#endif // MYLOG_ENABLED
}


SubsecondTime CXLVNServerCntlr::getVN(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now, ShmemPerf *perf){
   SubsecondTime cxl_latency = m_vn_perf_model->getAccessLatency(now, m_vn_length, requester, address, CXLCntlrInterface::VN_READ, perf);

   ++m_vn_reads;
   MYLOG("[%d]R @ %08lx latency %s", requester, address, itostr(cxl_latency.getNS()).c_str());

   return cxl_latency;
}


SubsecondTime CXLVNServerCntlr::updateVN(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now, ShmemPerf *perf){
    SubsecondTime cxl_latency = m_vn_perf_model->getAccessLatency(now, m_vn_length, requester, address, CXLCntlrInterface::VN_UPDATE, perf);

   ++m_vn_updates;
   MYLOG("[%d]U @ %08lx latency %s", requester, address, itostr(cxl_latency.getNS()).c_str());

   return cxl_latency;
}

boost::tuple<SubsecondTime, HitWhere::where_t> 
CXLVNServerCntlr::getDataFromCXL(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now, ShmemPerf *perf){
   SubsecondTime data_access_latency, vn_latency, mee_decrypt_latency, mee_mac_latency;
   SubsecondTime total_latency;
   HitWhere::where_t hit_where;
   /*     Fetch cipher text      |     fetch VN          */
   /*     decrypt cipher text    |                       */
   /*                      generate MAC                  */
   boost::tie(data_access_latency, hit_where) = m_cxl_cntlr->getDataFromCXL(address, requester, data_buf, now, perf);
   mee_decrypt_latency = m_mee->decryptData(address, requester, now + data_access_latency, perf);
   vn_latency = getVN(address, requester, data_buf, now, perf);
   total_latency = vn_latency > data_access_latency + mee_decrypt_latency ? vn_latency : data_access_latency + mee_decrypt_latency;
   mee_mac_latency = m_mee->genMAC(address, requester, now + total_latency, perf);
   total_latency += mee_mac_latency;
   
   return boost::make_tuple(total_latency, hit_where);
}

boost::tuple<SubsecondTime, HitWhere::where_t> 
CXLVNServerCntlr::putDataToCXL(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now){
   SubsecondTime data_access_latency, vn_latency, mee_encryption_latency, mee_mac_latency;
   SubsecondTime total_latency;
   HitWhere::where_t hit_where;

   /*     encrypt cipher text       |     fetch VN          */
   /*    write cipher text    */
   /*   generate MAC                                        */
   vn_latency = updateVN(address, requester, data_buf, now, NULL);
   mee_encryption_latency = m_mee->encryptData(address, requester, now + vn_latency);
   boost::tie(data_access_latency, hit_where) = m_cxl_cntlr->putDataToCXL(address, requester, data_buf, now + mee_encryption_latency);
   total_latency = vn_latency > mee_encryption_latency ? vn_latency : mee_encryption_latency;
   mee_mac_latency = m_mee->genMAC(address, requester, now + total_latency, &m_dummy_shmem_perf);

   total_latency =
       data_access_latency + mee_mac_latency > total_latency + mee_mac_latency
           ? data_access_latency + mee_mac_latency
           : total_latency + mee_mac_latency;

   return boost::make_tuple(total_latency, hit_where);
}

void CXLVNServerCntlr::enablePerfModel() 
{
   m_vn_perf_model->enable();
   m_mee->enablePerfModel();
   m_cxl_cntlr->enablePerfModel();
   m_enable_trace = true;
}

void CXLVNServerCntlr::disablePerfModel() 
{
   m_vn_perf_model->disable();
   m_mee->disablePerfModel();
   m_cxl_cntlr->disablePerfModel();
   m_enable_trace = false;
}