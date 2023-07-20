#include "cxl_vnserver_cntlr.h"
#include "stats.h"
#include "config.h"
#include "config.hpp"
#include "mee_naive.h"

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
      CXLAddressTranslator* cxl_address_tranlator, CXLCntlr* cxl_cntlr, core_id_t core_id):
    CXLCntlrInterface(memory_manager, shmem_perf_model, cache_block_size, cxl_address_tranlator),
    m_vnserver_cxl_id(cxl_address_tranlator->getnumCXLDevices()),
    m_vn_reads(0),
    m_vn_updates(0),
    m_cxl_cntlr(cxl_cntlr),
    f_trace(NULL),
    m_enable_trace(false)
{
   m_cxl_pkt_size = Sim()->getCfg()->getInt("perf_model/cxl/vnserver/pkt_size");
   m_mee = new MEENaive(memory_manager, shmem_perf_model, core_id + 1, cache_block_size, cxl_cntlr);
   m_vn_length = m_mee->getVNLength(); // vn_length in bits

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
   if (m_mee) delete m_mee;
#ifdef MYLOG_ENABLED
   fclose(f_trace);
#endif // MYLOG_ENABLED
}


SubsecondTime CXLVNServerCntlr::getVN(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now, ShmemPerf *perf){
   SubsecondTime cxl_latency = m_vn_perf_model->getAccessLatency(now, m_vn_length, requester, address, CXLCntlrInterface::VN_READ, perf);

   ++m_vn_reads;
   MYLOG("[%d]getVN @ %016lx latency %s", requester, address, itostr(cxl_latency.getNS()).c_str());

   return cxl_latency;
}


SubsecondTime CXLVNServerCntlr::updateVN(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now, ShmemPerf *perf){
    SubsecondTime cxl_latency = m_vn_perf_model->getAccessLatency(now, m_vn_length, requester, address, CXLCntlrInterface::VN_UPDATE, perf);

   ++m_vn_updates;
   MYLOG("[%d]UpdateVN @ %016lx latency %s", requester, address, itostr(cxl_latency.getNS()).c_str());

   return cxl_latency;
}

boost::tuple<SubsecondTime, HitWhere::where_t> 
CXLVNServerCntlr::getDataFromCXL(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now, ShmemPerf *perf){
   SubsecondTime vn_latency, mac_latency;
   SubsecondTime latency;
   HitWhere::where_t hit_where, hit_where_mac;
   MYLOG("[%d]R @ %016lx ", requester, address);

   boost::tie(latency, hit_where) = m_cxl_cntlr->getDataFromCXL(address, requester, data_buf, now, perf); // fetch data from CXL memory expander
   latency += m_mee->decryptData(address, requester, now + latency, perf); // decrypt cipher text
   boost::tie(mac_latency, hit_where_mac) = m_mee->verifyMAC(address, requester, now, perf); // verify MAC
   if (hit_where_mac != HitWhere::MEE_CACHE) { // meta data is not cached.
      vn_latency = getVN(address, requester, data_buf, now, perf); // fetch VN from VN Vault
   }  else {
      vn_latency = mac_latency;
   }

   /* latency is max of vn_latency, data_latency & mac latency */
   latency = mac_latency > latency ? mac_latency : latency;
   latency = vn_latency > latency ? vn_latency : latency;
   return boost::make_tuple(latency, hit_where);
}

boost::tuple<SubsecondTime, HitWhere::where_t> 
CXLVNServerCntlr::putDataToCXL(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now){
   SubsecondTime encryption_latency, vn_latency, mac_latency, latency;
   HitWhere::where_t hit_where, hit_where_mac;
   MYLOG("[%d]W @ %016lx ", requester, address);
   
   boost::tie(mac_latency, hit_where_mac) = m_mee->genMAC(address, requester, now); // access MAC
   vn_latency = updateVN(address, requester, data_buf, now, &m_dummy_shmem_perf); // always update VN in VN Vault, even when it is cached. 
   encryption_latency = m_mee->encryptData(address, requester, now + vn_latency); // encrypt data (is dependent on VN)
   boost::tie(latency, hit_where) = m_cxl_cntlr->putDataToCXL(address, requester, data_buf, now + encryption_latency + vn_latency); // put cipher text data to CXL
   return boost::make_tuple(SubsecondTime::Zero(), HitWhere::CXL);
}

SubsecondTime CXLVNServerCntlr::getVNFromCXL(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now, ShmemPerf *perf){
   return getVN(address, requester, data_buf, now, perf);
}

SubsecondTime CXLVNServerCntlr::updateVNToCXL(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now){
   return updateVN(address, requester, data_buf, now, NULL);
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