#include "cxl_invisimem_cntlr.h"
#include "config.h"
#include "config.hpp"
#include "stats.h"
#include "shmem_perf.h"
#include "mee_perf_model.h"

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
    m_metadata_per_cl(cache_block_size / Sim()->getCfg()->getInt("perf_model/mee/mac_per_cl")),  // in bytes
    m_cxl_pkt_size(Sim()->getCfg()->getInt("perf_model/cxl/pkt_size")),
    m_hmc_block_size(Sim()->getCfg()->getInt("perf_model/cxl/memory_expander_0/dram/block_size")),
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
     m_total_read_latency = (SubsecondTime*) malloc(sizeof(SubsecondTime)*cxl_connected.size());
     m_total_decrypt_latency = (SubsecondTime*) malloc(sizeof(SubsecondTime)*cxl_connected.size());
     m_cxl_perf_models = (CXLPerfModel**) malloc(sizeof(CXLPerfModel*)*cxl_connected.size());
     memset(m_cxl_perf_models, 0, sizeof(CXLPerfModel*) * cxl_connected.size());
     m_dram_perf_models = (DramPerfModel**) malloc(sizeof(DramPerfModel*)*cxl_connected.size());
     memset(m_dram_perf_models, 0, sizeof(DramPerfModel*) * cxl_connected.size());
     m_mee_perf_models = (MEEPerfModel**) malloc(sizeof(MEEPerfModel*)*cxl_connected.size());
     memset(m_mee_perf_models, 0, sizeof(MEEPerfModel*) * cxl_connected.size());
     for (cxl_id_t cxl_id = 0; cxl_id < cxl_connected.size(); ++cxl_id) {
         if (cxl_connected[cxl_id]) {
             /* Create CXL perf model */;
             m_cxl_perf_models[cxl_id] = CXLPerfModel::createCXLPerfModel(cxl_id, m_cxl_pkt_size * 8);  /* convert from bytes to bits */
             /* Create DRAM perf model on memory expanders */
             m_dram_perf_models[cxl_id] = DramPerfModel::createDramPerfModel(cxl_id, m_hmc_block_size, DramType::CXL_MEMORY); /* DramPerfModel takes bytes */
             /* Create MEE perf model */
             m_mee_perf_models[cxl_id] = new MEEPerfModel(cxl_id + 1);
            
             // stats
             m_total_read_latency[cxl_id] = SubsecondTime::Zero();
             m_total_decrypt_latency[cxl_id] = SubsecondTime::Zero();
             registerStatsMetric("cxl", cxl_id, "total-read-latency", &m_total_read_latency[cxl_id]);
             registerStatsMetric("cxl", cxl_id, "total-decrypt-latency", &m_total_decrypt_latency[cxl_id]);
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
   if (m_total_read_latency) free(m_total_read_latency);\
   if (m_total_decrypt_latency) free(m_total_decrypt_latency);

   for (size_t i = 0; i < m_cxl_connected.size(); ++i) {
       if (m_cxl_perf_models[i]) {
           delete m_cxl_perf_models[i];
           delete m_dram_perf_models[i];
           delete m_mee_perf_models[i];
       }
   }
   free(m_cxl_perf_models);
   free(m_dram_perf_models);
   free(m_mee_perf_models);

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
   IntPtr metadata_addr = m_address_translator->getMACAddrFromPhysical(local_address, cxl_id);
   

   UInt64 pkt_size = (m_read_req_size + m_read_res_size) * 8; /* Bytes to bits */
   LOG_ASSERT_ERROR(m_cxl_connected[cxl_id], "CXL %d is not connected", cxl_id);
   SubsecondTime cxl_latency = m_cxl_perf_models[cxl_id]->getAccessLatency(now, pkt_size, requester, local_address, READ, perf);

   // Access data 
   SubsecondTime dram_data_latency = m_dram_perf_models[cxl_id]->getAccessLatency(now + cxl_latency, getCacheBlockSize(), requester, local_address, DramCntlrInterface::access_t::READ, perf);
   // Access Meta data
   SubsecondTime dram_metadata_latency = m_dram_perf_models[cxl_id]->getAccessLatency(now + cxl_latency, m_metadata_per_cl, requester, metadata_addr, DramCntlrInterface::access_t::READ, perf);
   // fprintf(stderr, "[InvisiMem CXL Cntlr] READ data_addr: 0x%lx %d bytes metadata_addr: 0x%lx %dbytes\n", local_address,getCacheBlockSize(),  metadata_addr, m_metadata_per_cl);

   SubsecondTime access_latency = cxl_latency + (dram_data_latency > dram_metadata_latency? dram_data_latency : dram_metadata_latency);

   // AES decrypt
   SubsecondTime aes_lat = m_mee_perf_models[cxl_id]->getAESLatency(now + access_latency, requester, MEEBase::MEE_op_t::DECRYPT, perf);
   access_latency += aes_lat;

   

   ++m_reads[cxl_id];
   m_total_read_latency[cxl_id] += access_latency;
   m_total_decrypt_latency[cxl_id] += aes_lat;
   MYLOG(
       "[%d]R @ %016lx latency %s, cxl latency %s, dram_data %s, dram_metadata "
       "%s",
       cxl_id, address, itostr(access_latency.getNS()).c_str(),
       itostr(cxl_latency.getNS()).c_str(),
       itostr(dram_data_latency.getNS()).c_str(),
       itostr(dram_metadata_latency.getNS()).c_str());

   return boost::tuple<SubsecondTime, HitWhere::where_t>(access_latency, HitWhere::CXL);
}


boost::tuple<SubsecondTime, HitWhere::where_t>
CXLInvisiMemCntlr::putDataToCXL(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now, cxl_id_t cxl_id){
   IntPtr local_address = address;
   if (cxl_id == INVALID_CXL_ID){ // address is virtual address
      cxl_id = m_address_translator->getHome(address);
      local_address = m_address_translator->getPhyAddress(address);
   }
   IntPtr metadata_addr = m_address_translator->getMACAddrFromPhysical(local_address, cxl_id);

   UInt64 pkt_size = (m_write_req_size + m_write_res_size) * 8; /* Byte to bits */
   LOG_ASSERT_ERROR(m_cxl_connected[cxl_id], "CXL %d is not connected", cxl_id);

   SubsecondTime aes_latency = m_mee_perf_models[cxl_id]->getAESLatency(now, requester, MEEBase::MEE_op_t::ENCRYPT, &m_dummy_shmem_perf);

   SubsecondTime cxl_latency = m_cxl_perf_models[cxl_id]->getAccessLatency(now + aes_latency, pkt_size, requester, local_address, WRITE, &m_dummy_shmem_perf);

   // Write data 
   SubsecondTime dram_data_latency = m_dram_perf_models[cxl_id]->getAccessLatency(now + cxl_latency + aes_latency, getCacheBlockSize(), requester, local_address, DramCntlrInterface::access_t::WRITE, &m_dummy_shmem_perf);
   // Write Meta data
   SubsecondTime dram_metadata_latency = m_dram_perf_models[cxl_id]->getAccessLatency(now + cxl_latency + aes_latency, m_metadata_per_cl, requester, metadata_addr, DramCntlrInterface::access_t::WRITE, &m_dummy_shmem_perf);
   // fprintf(stderr, "[InvisiMem CXL Cntlr] data_addr: 0x%lx metadata_addr: 0x%lx\n", local_address, metadata_addr);

   SubsecondTime access_latency = cxl_latency + (dram_data_latency > dram_metadata_latency? dram_data_latency : dram_metadata_latency) + aes_latency;

   // Update stats
   ++m_writes[cxl_id];
   MYLOG("[%d]W @ %016lx latency %s", cxl_id, address, itostr(access_latency.getNS()).c_str());

   return boost::tuple<SubsecondTime, HitWhere::where_t>(access_latency, HitWhere::CXL);
}

void CXLInvisiMemCntlr::enablePerfModel() 
{
   for (cxl_id_t cxl_id = 0; cxl_id < m_cxl_connected.size(); cxl_id++){
      if (m_cxl_connected[cxl_id]){
         assert(m_cxl_perf_models[cxl_id]);
         m_cxl_perf_models[cxl_id]->enable();
         m_dram_perf_models[cxl_id]->enable();
         m_mee_perf_models[cxl_id]->enable();
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
         m_dram_perf_models[cxl_id]->disable();
         m_mee_perf_models[cxl_id]->disable();
      }
   } 
   enable_trace = false;
}