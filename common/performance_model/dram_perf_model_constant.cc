#include "dram_perf_model_constant.h"
#include "simulator.h"
#include "config.h"
#include "config.hpp"
#include "stats.h"
#include "shmem_perf.h"

#if 1
#define MYTRACE_ENABLED
   extern Lock iolock;
#include "core_manager.h"
#include "simulator.h"
#define MYTRACE(...)                                    \
{                                                       \
    ScopedLock l(iolock);                               \
    fflush(f_trace);                                    \
    fprintf(f_trace, "[%d DRAM] ", m_core_id);          \
    fprintf(f_trace, __VA_ARGS__);                      \
    fprintf(f_trace, "\n");                             \
    fflush(f_trace);                                    \
}
#else
#define MYTRACE(...) {}
#endif

DramPerfModelConstant::DramPerfModelConstant(core_id_t core_id,
      UInt32 cache_block_size, DramType dram_type):
   DramPerfModel(core_id, cache_block_size),
   m_queue_model(NULL),
   m_dram_bandwidth(8 * (dram_type == DramType::SYSTEM_DRAM ? 
                     Sim()->getCfg()->getFloat("perf_model/dram/per_controller_bandwidth") :
                     Sim()->getCfg()->getFloat("perf_model/cxl/memory_expander_" + itostr((unsigned int)core_id) + "/dram/per_controller_bandwidth"))), // Convert bytes to bits
   m_total_queueing_delay(SubsecondTime::Zero()),
   m_total_access_latency(SubsecondTime::Zero())
{
   m_dram_access_cost = SubsecondTime::FS() * static_cast<uint64_t>(TimeConverter<float>::NStoFS(Sim()->getCfg()->getFloat("perf_model/dram/latency"))); // Operate in fs for higher precision before converting to uint64_t/SubsecondTime

   if (dram_type == DramType::SYSTEM_DRAM ? 
      Sim()->getCfg()->getBool("perf_model/dram/queue_model/enabled")
      : Sim()->getCfg()->getBool("perf_model/cxl/memory_expander_" + itostr((unsigned int)core_id) + "/dram/queue_model/enabled"))
   {
      m_queue_model = dram_type == DramType::SYSTEM_DRAM ? 
                        QueueModel::create("dram-queue", core_id, Sim()->getCfg()->getString("perf_model/dram/queue_model/type"),
                                         m_dram_bandwidth.getRoundedLatency(8 * cache_block_size)) // bytes to bits
                        :QueueModel::create("cxl-dram-queue", core_id, Sim()->getCfg()->getString("perf_model/cxl/memory_expander_" + itostr((unsigned int)core_id) + "/dram/queue_model/type"),
                                         m_dram_bandwidth.getRoundedLatency(8 * cache_block_size)); // bytes to bits
   }

   if (dram_type == DramType::SYSTEM_DRAM) {
      registerStatsMetric("dram", core_id, "total-access-latency", &m_total_access_latency);
      registerStatsMetric("dram", core_id, "total-queueing-delay", &m_total_queueing_delay);
   } else if (dram_type == DramType::CXL_MEMORY){
      registerStatsMetric("cxl-dram", core_id, "total-access-latency", &m_total_access_latency);
      registerStatsMetric("cxl-dram", core_id, "total-queueing-delay", &m_total_queueing_delay);
   } else {
      std::cerr << "Unsupported dram type " << dram_type << std::endl;
   }
#ifdef MYTRACE_ENABLED
   std::ostringstream trace_filename;
   if (dram_type == DramType::SYSTEM_DRAM){
      trace_filename << "dram_perf_" << m_core_id << ".trace";
   } else if (dram_type == DramType::CXL_MEMORY){
      trace_filename << "cxl_dram_perf_" << m_core_id << ".trace";
   }
   f_trace = fopen(trace_filename.str().c_str(), "w+");
   std::cerr << "Create DRAM perf trace " << trace_filename.str().c_str() << std::endl;
#endif // MYTRACE_ENABLED
}

DramPerfModelConstant::~DramPerfModelConstant()
{
   if (m_queue_model)
   {
     delete m_queue_model;
      m_queue_model = NULL;
   }
}

SubsecondTime
DramPerfModelConstant::getAccessLatency(SubsecondTime pkt_time, UInt64 pkt_size, core_id_t requester, IntPtr address, DramCntlrInterface::access_t access_type, ShmemPerf *perf)
{
   // pkt_size is in 'Bytes'
   // m_dram_bandwidth is in 'Bits per clock cycle'
   if ((!m_enabled) ||
         (requester >= (core_id_t) Config::getSingleton()->getApplicationCores()))
   {
      return SubsecondTime::Zero();
   }

   SubsecondTime processing_time = m_dram_bandwidth.getRoundedLatency(8 * pkt_size); // bytes to bits

   // Compute Queue Delay
   SubsecondTime queue_delay;
   if (m_queue_model)
   {
      queue_delay = m_queue_model->computeQueueDelay(pkt_time, processing_time, requester);
   }
   else
   {
      queue_delay = SubsecondTime::Zero();
   }

   SubsecondTime access_latency = queue_delay + processing_time + m_dram_access_cost;

   switch(access_type){
      case DramCntlrInterface::READ:
         MYTRACE("0x%016lx\tREAD\t%lu\t%lu\t%lu", address, access_latency.getNS(), processing_time.getNS(), queue_delay.getNS());
         break;
      case DramCntlrInterface::WRITE:
         MYTRACE("W ==%s== @ %016lx", itostr(pkt_time + queue_delay + processing_time).c_str(), address);
         break;
      default:
         LOG_ASSERT_ERROR(false, "Unsupported DramCntlrInterface::access_t(%u)", access_type);
   }
   
   perf->updateTime(pkt_time);
   perf->updateTime(pkt_time + queue_delay, ShmemPerf::DRAM_QUEUE);
   perf->updateTime(pkt_time + queue_delay + processing_time, ShmemPerf::DRAM_BUS);
   perf->updateTime(pkt_time + queue_delay + processing_time + m_dram_access_cost, ShmemPerf::DRAM_DEVICE);

   // Update Memory Counters
   m_num_accesses ++;
   m_total_access_latency += access_latency;
   m_total_queueing_delay += queue_delay;

   return access_latency;
}
