#include "dram_perf_model_dramsim.h"
#include "hooks_manager.h"
#include "simulator.h"
#include "config.h"
#include "config.hpp"
#include "stats.h"
#include "shmem_perf.h"
#include "shmem_msg.h"
#include "memory_manager.h"

#if 0
#define MYTRACE_ENABLED
   extern Lock iolock;
#include "core_manager.h"
#define MYTRACE(...)                                    \
{ if (m_enabled){                                          \
    ScopedLock l(iolock);                               \
    fflush(f_trace);                                    \
    fprintf(f_trace, "[%d DRAM] ", m_core_id);          \
    fprintf(f_trace, __VA_ARGS__);                      \
    fprintf(f_trace, "\n");                             \
    fflush(f_trace);                                    \
}}
#else
#define MYTRACE(...) {}
#endif

DramPerfModelDramSim::DramPerfModelDramSim(core_id_t core_id,
      UInt32 cache_block_size):
   DramPerfModel(core_id, cache_block_size),
   m_queue_model(NULL),
   m_dram_bandwidth(8 * Sim()->getCfg()->getFloat("perf_model/dram/per_controller_bandwidth")), // Convert bytes to bits
   m_cache_block_size(cache_block_size),
   m_total_queueing_delay(SubsecondTime::Zero()),
   m_total_access_latency(SubsecondTime::Zero()),
   m_dramsim(NULL),
   m_dramsim_channels(Sim()->getCfg()->getInt("perf_model/dram/dramsim/channles_per_contoller"))
   
{
   Sim()->getHooksManager()->registerHook(HookType::HOOK_INSTRUMENT_MODE, DramPerfModelDramSim::Change_mode_HOOK, (UInt64)this);
   
   m_dram_access_cost = SubsecondTime::FS() * static_cast<uint64_t>(TimeConverter<float>::NStoFS(Sim()->getCfg()->getFloat("perf_model/dram/latency"))); // Operate in fs for higher precision before converting to uint64_t/SubsecondTime

   if (Sim()->getCfg()->getBool("perf_model/dram/queue_model/enabled"))
   {
      m_queue_model = QueueModel::create("dram-queue", core_id, Sim()->getCfg()->getString("perf_model/dram/queue_model/type"),
                                         m_dram_bandwidth.getRoundedLatency(8 * cache_block_size)); // bytes to bits
   }

   /* Initilize DRAMsim3 simulator */
   m_dramsim = (DRAMsimCntlr**)malloc(sizeof(DRAMsimCntlr*) * m_dramsim_channels);
   for (UInt32 ch_id = 0; ch_id < m_dramsim_channels; ch_id++){
      m_dramsim[ch_id] = new DRAMsimCntlr(core_id, ch_id);
   }

   registerStatsMetric("dram", core_id, "total-access-latency", &m_total_access_latency);
   registerStatsMetric("dram", core_id, "total-queueing-delay", &m_total_queueing_delay);
#ifdef MYTRACE_ENABLED
   std::ostringstream trace_filename;
   trace_filename << "dram_perf_" << m_core_id << ".trace";
   f_trace = fopen(trace_filename.str().c_str(), "w+");
    std::cerr << "Create DRAM perf trace " << trace_filename.str().c_str() << std::endl;
#endif // MYTRACE_ENABLED
}

DramPerfModelDramSim::~DramPerfModelDramSim()
{
   if (m_queue_model)
   {
     delete m_queue_model;
      m_queue_model = NULL;
   }

   if (m_dramsim)
   {
      for (UInt32 ch_id = 0; ch_id < m_dramsim_channels; ch_id++){
         delete m_dramsim[ch_id];
      }
      free(m_dramsim);
      m_dramsim = NULL;
   }
}

SubsecondTime
DramPerfModelDramSim::getAccessLatency(SubsecondTime pkt_time, UInt64 pkt_size, core_id_t requester, IntPtr address, DramCntlrInterface::access_t access_type, ShmemPerf *perf)
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

   uint32_t dramsim_ch_id = (address/m_cache_block_size) % m_dramsim_channels;
   IntPtr ch_addr = (address/m_cache_block_size) / m_dramsim_channels * m_cache_block_size;
   m_dramsim[dramsim_ch_id]->addTrans(pkt_time + queue_delay, ch_addr, access_type == DramCntlrInterface::WRITE);

   switch(access_type){
      case DramCntlrInterface::READ:
         MYTRACE("R ==%s== @ %016lx", itostr(pkt_time + queue_delay + processing_time).c_str(), address);
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
void DramPerfModelDramSim::dramsimAdvance(SubsecondTime barrier_time){
   for (UInt32 ch_id = 0; ch_id < m_dramsim_channels; ch_id++){
      m_dramsim[ch_id]->advance(barrier_time);
   }
}

void DramPerfModelDramSim::dramsimStart(){
   Sim()->getHooksManager()->registerHook(HookType::HOOK_PERIODIC, DramPerfModelDramSim::Periodic_HOOK, (UInt64)this);
   for (UInt32 ch_id = 0; ch_id < m_dramsim_channels; ch_id++){
      m_dramsim[ch_id]->start();
   }
}

void DramPerfModelDramSim::dramsimEnd(){
   for (UInt32 ch_id = 0; ch_id < m_dramsim_channels; ch_id++){
      m_dramsim[ch_id]->stop();
   }  
}

