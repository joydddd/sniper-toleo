#include "cxl_invisimem_cntlr.h"
#include "config.h"
#include "config.hpp"
#include "stats.h"
#include "shmem_perf.h"

#if 0
#  define MYLOG_ENABLED
   extern Lock iolock;
#  include "core_manager.h"
#  include "simulator.h"
#  define MYLOG(...) if (enable_trace){                                                   \
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

CXLInvisiMemCntlr::CXLInvisiMemCntlr(MemoryManagerBase* memory_manager, ShmemPerfModel* shmem_perf_model, UInt32 cache_block_size, CXLAddressTranslator* cxl_address_tranlator, std::vector<bool>& cxl_connected):
    CXLCntlrInterface(memory_manager, shmem_perf_model, cache_block_size, cxl_address_tranlator),
    m_read_req_size(Sim()->getCfg()->getInt("perf_model/mee/r_req_pkt_size")),
    m_read_res_size(Sim()->getCfg()->getInt("perf_model/mee/r_res_pkt_size")),
    m_write_req_size(Sim()->getCfg()->getInt("perf_model/mee/w_req_pkt_size")),
    m_write_res_size(Sim()->getCfg()->getInt("perf_model/mee/w_res_pkt_size")),
    m_cxl_connected(cxl_connected),
    m_reads(NULL),
    m_writes(NULL),
    f_trace(NULL),
    enable_trace(false)
{
     m_reads = (UInt64*) malloc(sizeof(UInt64)*cxl_connected.size());
     memset(m_reads, 0, sizeof(UInt64)*cxl_connected.size());
     m_writes = (UInt64*) malloc(sizeof(UInt64)*cxl_connected.size());
     memset(m_writes, 0, sizeof(UInt64)*cxl_connected.size());
     m_cxl_perf_models = (CXLPerfModel**) malloc(sizeof(CXLPerfModel*)*cxl_connected.size());
     memset(m_cxl_perf_models, 0, sizeof(CXLPerfModel*) * cxl_connected.size());
     for (cxl_id_t cxl_id = 0; cxl_id < cxl_connected.size(); ++cxl_id) {
         if (cxl_connected[cxl_id]) {
             /* Create CXL perf model */;
             m_cxl_perf_models[cxl_id] = CXLPerfModel::createCXLPerfModel(cxl_id, cache_block_size * 8); 
             /* convert from bytes to bits*/
             registerStatsMetric("cxl", cxl_id, "reads", &m_reads[cxl_id]);
             registerStatsMetric("cxl", cxl_id, "writes", &m_writes[cxl_id]);
         }
     }

#ifdef MYLOG_ENABLED
   std::ostringstream trace_filename;
   trace_filename << "cxl_cntlr_" << memory_manager->getCore()->getId()
                  << ".trace";
   f_trace = fopen(trace_filename.str().c_str(), "w+");
   std::cerr << "Create CXL cntlr trace " << trace_filename.str().c_str() << std::endl;
#endif // MYLOG_ENABLED
}


CXLInvisiMemCntlr::~CXLInvisiMemCntlr()
{
   if (m_reads) free(m_reads);
   if (m_writes) free(m_writes);

   for (size_t i = 0; i < m_cxl_connected.size(); ++i) {
       if (m_cxl_perf_models[i]) {
           delete m_cxl_perf_models[i];
       }
   }
   free(m_cxl_perf_models);

#ifdef MYLOG_ENABLED
   fclose(f_trace);
#endif // MYLOG_ENABLED
}

// if cxl_id is specified, address is virtual. Otherwise address is physical. 
boost::tuple<SubsecondTime, HitWhere::where_t> CXLInvisiMemCntlr::getDataFromCXL(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now, ShmemPerf *perf, cxl_id_t cxl_id){
   IntPtr local_address = address;
   if (cxl_id == INVALID_CXL_ID){ // address is virtual address
      cxl_id = m_address_translator->getHome(address);
      local_address = m_address_translator->getPhyAddress(address);
   } 
   UInt64 pkt_size = getCacheBlockSize() * 8; /* Byte to bits */

   LOG_ASSERT_ERROR(m_cxl_connected[cxl_id], "CXL %d is not connected", cxl_id);
   SubsecondTime cxl_latency = m_cxl_perf_models[cxl_id]->getAccessLatency(now, pkt_size, requester, local_address, READ, perf);

   ++m_reads[cxl_id];
   MYLOG("[%d]R @ %016lx latency %s", cxl_id, address, itostr(cxl_latency.getNS()).c_str());

   return boost::tuple<SubsecondTime, HitWhere::where_t>(cxl_latency, HitWhere::CXL);
}


boost::tuple<SubsecondTime, HitWhere::where_t>
CXLInvisiMemCntlr::putDataToCXL(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now, cxl_id_t cxl_id){
   IntPtr local_address = address;
   if (cxl_id == INVALID_CXL_ID){ // address is virtual address
      cxl_id = m_address_translator->getHome(address);
      local_address = m_address_translator->getPhyAddress(address);
   } 
   UInt64 pkt_size = getCacheBlockSize() * 8;/* Byte to bits */

   LOG_ASSERT_ERROR(m_cxl_connected[cxl_id], "CXL %d is not connected", cxl_id);
   SubsecondTime cxl_latency = m_cxl_perf_models[cxl_id]->getAccessLatency(now, pkt_size, requester, local_address, WRITE, &m_dummy_shmem_perf);

   ++m_writes[cxl_id];
   MYLOG("[%d]W @ %016lx latency %s", cxl_id, address, itostr(cxl_latency.getNS()).c_str());

   return boost::tuple<SubsecondTime, HitWhere::where_t>(cxl_latency, HitWhere::CXL);
}

void CXLInvisiMemCntlr::enablePerfModel() 
{
   for (cxl_id_t cxl_id = 0; cxl_id < m_cxl_connected.size(); cxl_id++){
      if (m_cxl_connected[cxl_id]){
         assert(m_cxl_perf_models[cxl_id]);
         m_cxl_perf_models[cxl_id]->enable();
      }
   }
   enable_trace = true;
}

void CXLInvisiMemCntlr::disablePerfModel() 
{
   for (cxl_id_t cxl_id = 0; cxl_id < m_cxl_connected.size(); cxl_id++){
      if (m_cxl_connected[cxl_id]){
         assert(m_cxl_perf_models[cxl_id]);
         m_cxl_perf_models[cxl_id]->disable();
      }
   } 
   enable_trace = false;
}