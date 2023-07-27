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
    m_data_reads(NULL), m_data_writes(NULL),
    m_mac_reads(NULL), m_mac_writes(NULL),
    m_cxl_cntlr(cxl_cntlr),
    f_trace(NULL),
    m_enable_trace(false)
{
   m_cxl_pkt_size = Sim()->getCfg()->getInt("perf_model/cxl/vnserver/pkt_size");
   m_mee = new MEENaive(memory_manager, shmem_perf_model, core_id + 1, cache_block_size, this);
   m_vn_length = m_mee->getVNLength(); // vn_length in bits

   m_vn_perf_model = CXLPerfModel::createCXLPerfModel(0, m_cxl_pkt_size);
   registerStatsMetric("vn-vault", 0, "reads", &m_vn_reads);
   registerStatsMetric("vn-vault", 0, "updates", &m_vn_updates);
   
   //Initialize stats
   m_data_reads = (UInt64*) malloc(sizeof(UInt64)*cxl_address_tranlator->getnumCXLDevices());
   memset(m_data_reads, 0, sizeof(UInt64)*cxl_address_tranlator->getnumCXLDevices());
   m_data_writes = (UInt64*) malloc(sizeof(UInt64)*cxl_address_tranlator->getnumCXLDevices());
   memset(m_data_writes, 0, sizeof(UInt64)*cxl_address_tranlator->getnumCXLDevices());
   m_mac_reads = (UInt64*) malloc(sizeof(UInt64)*cxl_address_tranlator->getnumCXLDevices());
   memset(m_mac_reads, 0, sizeof(UInt64)*cxl_address_tranlator->getnumCXLDevices());
   m_mac_writes = (UInt64*) malloc(sizeof(UInt64)*cxl_address_tranlator->getnumCXLDevices());
   memset(m_mac_writes, 0, sizeof(UInt64)*cxl_address_tranlator->getnumCXLDevices());
   for (cxl_id_t cxl_id = 0; cxl_id < cxl_address_tranlator->getnumCXLDevices();
        cxl_id++) {
       if (m_cxl_cntlr->m_cxl_connected[cxl_id]) {
           registerStatsMetric("cxl", cxl_id, "data-reads",
                               &m_data_reads[cxl_id]);
           registerStatsMetric("cxl", cxl_id, "data-writes",
                               &m_data_writes[cxl_id]);
           registerStatsMetric("cxl", cxl_id, "mac-reads",
                               &m_mac_reads[cxl_id]);
           registerStatsMetric("cxl", cxl_id, "mac-writes",
                               &m_mac_writes[cxl_id]);
       }
   }

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
   if (m_data_reads) free(m_data_reads);
   if (m_data_writes) free(m_data_writes);
   if (m_mac_reads) free(m_mac_reads);
   if (m_mac_writes) free(m_mac_writes);
   if (m_vn_perf_model) delete m_vn_perf_model;
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

SubsecondTime CXLVNServerCntlr::getMAC(IntPtr mac_addr, core_id_t requester, Byte* data_buf, SubsecondTime now, ShmemPerf *perf){
   SubsecondTime cxl_latency;
   HitWhere::where_t hit_where;
   boost::tie(cxl_latency, hit_where) = m_cxl_cntlr->getDataFromCXL(mac_addr, requester, data_buf, now, perf);
   LOG_ASSERT_ERROR(hit_where == HitWhere::CXL, "MAC %lu should be in CXL memory expander", mac_addr);

   cxl_id_t cxl_id = m_address_translator->getHome(mac_addr);
   ++m_mac_reads[cxl_id];
   MYLOG("[%d]getMAC @ %016lx latency %s", requester, mac_addr, itostr(cxl_latency.getNS()).c_str());
   return cxl_latency;
}

SubsecondTime CXLVNServerCntlr::putMAC(IntPtr mac_addr, core_id_t requester, Byte* data_buf, SubsecondTime now){
   SubsecondTime cxl_latency;
   HitWhere::where_t hit_where;
   boost::tie(cxl_latency, hit_where) = m_cxl_cntlr->putDataToCXL(mac_addr, requester, data_buf, now);
   LOG_ASSERT_ERROR(hit_where == HitWhere::CXL, "MAC %lu should be in CXL memory expander", mac_addr);

   cxl_id_t cxl_id = m_address_translator->getHome(mac_addr);
   ++m_mac_writes[cxl_id];
   MYLOG("[%d]putMAC @ %016lx", requester, mac_addr);
   return SubsecondTime::Zero();
}

boost::tuple<SubsecondTime, HitWhere::where_t> 
CXLVNServerCntlr::getDataFromCXL(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now, ShmemPerf *perf){
   SubsecondTime vn_latency, mac_latency, verify_latency, decrypt_latency;
   SubsecondTime latency;
   HitWhere::where_t hit_where, hit_where_mac;
  

   boost::tie(latency, hit_where) = m_cxl_cntlr->getDataFromCXL(address, requester, data_buf, now, perf); // fetch data from CXL memory expander
   LOG_ASSERT_ERROR(hit_where == HitWhere::CXL, "Data %lu should be in CXL memory expander", address);
   boost::tie(mac_latency, vn_latency, hit_where_mac) = m_mee->fetchMACVN(address, requester, now, perf); // fetch MAC & VN. (MEE_CACHE, or CXL, CXL_VN)
   
   decrypt_latency = m_mee->decryptData(address, requester, now + vn_latency, perf); // decrypt cipher text (dependent on VN)
   latency = decrypt_latency + vn_latency > latency ? decrypt_latency + vn_latency : latency;
   
   verify_latency = m_mee->verifyMAC(address, requester, now + latency, perf); // verify MAC
   latency = verify_latency + latency > mac_latency ? verify_latency + latency : mac_latency;
   
   cxl_id_t cxl_id = m_address_translator->getHome(address);
   ++m_data_reads[cxl_id];
   MYLOG("[%d]R @ %016lx latency %s", requester, address, itostr(latency.getNS()).c_str());
   return boost::make_tuple(latency, hit_where_mac);
}

boost::tuple<SubsecondTime, HitWhere::where_t> 
CXLVNServerCntlr::putDataToCXL(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now){
   SubsecondTime encryption_latency, vn_latency, mac_latency, latency;
   HitWhere::where_t hit_where, hit_where_mac;

   cxl_id_t cxl_id = m_address_translator->getHome(address);
   ++m_data_writes[cxl_id];
   MYLOG("[%d]W @ %016lx ", requester, address);
   
   vn_latency = updateVN(address, requester, data_buf, now, &m_dummy_shmem_perf); // always update VN in VN Vault, even when it is cached. 
   encryption_latency = m_mee->encryptData(address, requester, now + vn_latency); // encrypt data (is dependent on VN)
   boost::tie(latency, hit_where) = m_cxl_cntlr->putDataToCXL(address, requester, data_buf, now + encryption_latency + vn_latency); // put cipher text data to CXL
   LOG_ASSERT_ERROR(hit_where == HitWhere::CXL, "Data %lu should be in CXL memory expander", address);
   
   boost::tie(mac_latency, hit_where_mac) = m_mee->genMAC(address, requester, now + encryption_latency + vn_latency); // generate MAC

   return boost::make_tuple(SubsecondTime::Zero(), hit_where_mac);
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