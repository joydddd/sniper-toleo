#include "dram_cntlr.h"
#include "memory_manager.h"
#include "core.h"
#include "log.h"
#include "subsecond_time.h"
#include "stats.h"
#include "fault_injection.h"
#include "shmem_perf.h"
#include "config.h"
#include "config.hpp"

#if 0
#  define MYLOG_ENABLED
   extern Lock iolock;
#  include "core_manager.h"
#  include "simulator.h"
#  include "magic_server.h"
#  define MYLOG(...) { if (Sim()->getMagicServer()->inROI()){                             \
   /*std::cerr << "WRITE " << std::endl;  */                                                  \
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

#if 1
# define TRACE_ANALYSIS_ENABLED
   extern Lock iolock;
#  include "dram_trace_analysis.h"
#  define TRACE(core, addr, type){ \
   Sim()->getDramTraceAnalyzer()->RecordDramAccess(core, addr, type); \
}

#else
#define TRACE(core, addr, type){}
#endif 

class TimeDistribution;

namespace PrL1PrL2DramDirectoryMSI
{

DramCntlr::DramCntlr(MemoryManagerBase* memory_manager,
      ShmemPerfModel* shmem_perf_model,
      UInt32 cache_block_size,
      CXLAddressTranslator* cxl_address_translator)
   : DramCntlrInterface(memory_manager, shmem_perf_model, cache_block_size, cxl_address_translator)
   , m_reads(0)
   , m_writes(0)
{
   m_dram_perf_model = DramPerfModel::createDramPerfModel(
         memory_manager->getCore()->getId(),
         cache_block_size);

   m_fault_injector = Sim()->getFaultinjectionManager()
      ? Sim()->getFaultinjectionManager()->getFaultInjector(memory_manager->getCore()->getId(), MemComponent::DRAM)
      : NULL;

   m_dram_access_count = new AccessCountMap[DramCntlrInterface::NUM_ACCESS_TYPES];
   registerStatsMetric("dram", memory_manager->getCore()->getId(), "reads",
                     &m_reads);
   registerStatsMetric("dram", memory_manager->getCore()->getId(), "writes",
                     &m_writes);

#ifdef MYLOG_ENABLED
   std::ostringstream trace_filename;
   trace_filename << "dram_cntlr_" << memory_manager->getCore()->getId()
                  << ".trace";
   f_trace = fopen(trace_filename.str().c_str(), "w+");
   std::cerr << "Create Dram cntlr trace " << trace_filename.str().c_str() << std::endl;
#endif // MYLOG_ENABLED
}

DramCntlr::~DramCntlr()
{
   printDramAccessCount();
   delete [] m_dram_access_count;

   delete m_dram_perf_model;
#ifdef MYLOG_ENABLED
   fclose(f_trace);
#endif // MYLOG_ENABLED
}

boost::tuple<SubsecondTime, HitWhere::where_t>
DramCntlr::getDataFromDram(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now, ShmemPerf *perf, bool is_virtual_addr)
{
   if (Sim()->getFaultinjectionManager())
   {
      if (m_data_map.count(address) == 0)
      {
         m_data_map[address] = new Byte[getCacheBlockSize()];
         memset((void*) m_data_map[address], 0x00, getCacheBlockSize());
      }

      // NOTE: assumes error occurs in memory. If we want to model bus errors, insert the error into data_buf instead
      if (m_fault_injector)
         m_fault_injector->preRead(address, address, getCacheBlockSize(), (Byte*)m_data_map[address], now);

      memcpy((void*) data_buf, (void*) m_data_map[address], getCacheBlockSize());
   }

   SubsecondTime dram_access_latency = runDramPerfModel(requester, now, address, READ, perf, is_virtual_addr);

   ++m_reads;
   #ifdef ENABLE_DRAM_ACCESS_COUNT
   addToDramAccessCount(address, READ);
   #endif
   MYLOG("[%d]R @ %016lx latency %s", requester, address, itostr(dram_access_latency.getNS()).c_str());
   TRACE(requester, address, DramCntlrInterface::READ);

   return boost::tuple<SubsecondTime, HitWhere::where_t>(dram_access_latency, HitWhere::DRAM);
}

boost::tuple<SubsecondTime, HitWhere::where_t>
DramCntlr::putDataToDram(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now, bool is_virtual_addr)
{
   if (Sim()->getFaultinjectionManager())
   {
      if (m_data_map[address] == NULL)
      {
         LOG_PRINT_ERROR("Data Buffer does not exist");
      }
      memcpy((void*) m_data_map[address], (void*) data_buf, getCacheBlockSize());

      // NOTE: assumes error occurs in memory. If we want to model bus errors, insert the error into data_buf instead
      if (m_fault_injector)
         m_fault_injector->postWrite(address, address, getCacheBlockSize(), (Byte*)m_data_map[address], now);
   }

   SubsecondTime dram_access_latency = runDramPerfModel(requester, now, address, WRITE, &m_dummy_shmem_perf, is_virtual_addr);

   ++m_writes;
#ifdef ENABLE_DRAM_ACCESS_COUNT
   addToDramAccessCount(address, WRITE);
   #endif
   MYLOG("[%d]W @ %016lx", requester, address);
   TRACE(requester, address, DramCntlrInterface::WRITE);

   return boost::tuple<SubsecondTime, HitWhere::where_t>(dram_access_latency, HitWhere::DRAM);
}

SubsecondTime
DramCntlr::runDramPerfModel(core_id_t requester, SubsecondTime time, IntPtr address, DramCntlrInterface::access_t access_type, ShmemPerf *perf, bool is_virtual_addr)
{
   UInt64 pkt_size = getCacheBlockSize();
   IntPtr phy_addr = is_virtual_addr && m_address_translator
                         ? m_address_translator->getPhyAddress(address)
                         : address;
   SubsecondTime dram_access_latency = m_dram_perf_model->getAccessLatency(
       time, pkt_size, requester, phy_addr, access_type, perf);
   return dram_access_latency;
}

void
DramCntlr::addToDramAccessCount(IntPtr address, DramCntlrInterface::access_t access_type)
{
   m_dram_access_count[access_type][address] = m_dram_access_count[access_type][address] + 1;
}

void
DramCntlr::printDramAccessCount()
{
   for (UInt32 k = 0; k < DramCntlrInterface::NUM_ACCESS_TYPES; k++)
   {
      for (AccessCountMap::iterator i = m_dram_access_count[k].begin(); i != m_dram_access_count[k].end(); i++)
      {
         if ((*i).second > 100)
         {
            LOG_PRINT("Dram Cntlr(%i), Address(0x%x), Access Count(%llu), Access Type(%s)",
                  m_memory_manager->getCore()->getId(), (*i).first, (*i).second,
                  (k == READ)? "READ" : "WRITE");
         }
      }
   }
}

}
