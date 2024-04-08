#include "dram_perf_model_ddr.h"
#include "simulator.h"
#include "config.h"
#include "config.hpp"
#include "stats.h"
#include "shmem_perf.h"

#if 0
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

DramPerfModelDDR::DramPerfModelDDR(core_id_t core_id, UInt32 cache_block_size):
   DramPerfModel(core_id, cache_block_size),
   m_ddr_burst_size(Sim()->getCfg()->getInt("perf_model/dram/ddr/burst_size")), // in bytes
   m_queue_model(NULL),
   m_dram_bandwidth(8 * Sim()->getCfg()->getFloat("perf_model/dram/per_controller_bandwidth")), // Convert bytes to bits / ns
   m_total_queueing_delay(SubsecondTime::Zero()),
   m_total_access_latency(SubsecondTime::Zero()),
   m_total_bytes_trans(0)
{
   m_queue_model = QueueModel::create("ddr-queue", core_id, Sim()->getCfg()->getString("perf_model/dram/ddr/queue_type"),
                                         m_dram_bandwidth.getRoundedLatency(8 * m_ddr_burst_size)); // bytes to bits
   registerStatsMetric("ddr", core_id, "total-access-latency", &m_total_access_latency);
   registerStatsMetric("ddr", core_id, "total-queueing-delay", &m_total_queueing_delay);
   registerStatsMetric("ddr", core_id, "total-bytes-trans", &m_total_bytes_trans);
#ifdef MYTRACE_ENABLED
   std::ostringstream trace_filename;
   trace_filename << "ddr_perf_" << m_core_id << ".trace";
   f_trace = fopen(trace_filename.str().c_str(), "w+");
   std::cerr << "Create DRAM perf trace " << trace_filename.str().c_str() << std::endl;
#endif // MYTRACE_ENABLED
}

DramPerfModelDDR::~DramPerfModelDDR()
{
   if (m_queue_model)
   {
     delete m_queue_model;
      m_queue_model = NULL;
   }
}

SubsecondTime
DramPerfModelDDR::getAccessLatency(SubsecondTime pkt_time, UInt64 pkt_size /* in bytes */, core_id_t requester, IntPtr address, DramCntlrInterface::access_t access_type, ShmemPerf *perf)
{
   // pkt_size is in 'Bytes'
   // m_dram_bandwidth is in 'Bits per clock cycle'
   if ((!m_enabled) ||
         (requester >= (core_id_t) Config::getSingleton()->getApplicationCores()))
   {
      return SubsecondTime::Zero();
   }

   // round up pkt_size to brust size
   pkt_size = (pkt_size + m_ddr_burst_size - 1) / m_ddr_burst_size * m_ddr_burst_size;

   SubsecondTime processing_time = m_dram_bandwidth.getRoundedLatency(8 * pkt_size); // bytes to bits

   // Compute Queue Delay
   SubsecondTime queue_delay;
   queue_delay = m_queue_model->computeQueueDelay(pkt_time, processing_time, requester);

   SubsecondTime ddr_latency = queue_delay + processing_time;

   switch(access_type){
      case DramCntlrInterface::READ:
         MYTRACE("0x%016lx\tREAD\t%lu\t%lu\t%lu\t%lu", address, pkt_time.getNS(), ddr_latency.getNS(), processing_time.getNS(), queue_delay.getNS());
         break;
      case DramCntlrInterface::WRITE:
         MYTRACE("W ==%s== @ %016lx", itostr(pkt_time + queue_delay + processing_time).c_str(), address);
         break;
      default:
         LOG_ASSERT_ERROR(false, "Unsupported DramCntlrInterface::access_t(%u)", access_type);
   }
   
   perf->updateTime(pkt_time);
   perf->updateTime(pkt_time + queue_delay, ShmemPerf::DDR_QUEUE);
   perf->updateTime(pkt_time + queue_delay + processing_time, ShmemPerf::DDR_BUS);

   // Update Memory Counters
   m_num_accesses ++;
   m_total_access_latency += ddr_latency;
   m_total_queueing_delay += queue_delay;
   m_total_bytes_trans += pkt_size;

   return ddr_latency;
}
