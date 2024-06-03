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
      UInt32 cache_block_size, DramType dram_type):
   DramPerfModel(core_id, cache_block_size),
   m_config_prefix(dram_type == DramType::SYSTEM_DRAM ? 
                     "perf_model/dram/" :
                     dram_type == DramType::CXL_MEMORY ?
                     "perf_model/cxl/memory_expander_" + itostr((unsigned int)core_id) + "/dram/" :
                     "perf_model/cxl/vnserver/dram/"),
   m_cache_block_size(cache_block_size),
   m_total_access_latency(SubsecondTime::Zero()),
   m_total_bp_latency(SubsecondTime::Zero()),
   m_total_read_latency(SubsecondTime::Zero()),
   m_dramsim(NULL), m_backpressure_queue(NULL),
   m_dramsim_channels(Sim()->getCfg()->getInt(m_config_prefix + "dramsim/channles_per_contoller")),
   m_dramsim_buffering_delay(SubsecondTime::Zero()), m_burst_processing_time(SubsecondTime::Zero()),
   m_bp_factor(1.0f), m_dram_burst_size(0)
{
   Sim()->getHooksManager()->registerHook(HookType::HOOK_INSTRUMENT_MODE, DramPerfModelDramSim::Change_mode_HOOK, (UInt64)this);
   
   m_dram_access_cost = SubsecondTime::FS() * static_cast<uint64_t>(TimeConverter<float>::NStoFS(Sim()->getCfg()->getFloat(m_config_prefix + "default_latency"))); 
   // Operate in fs for higher precision before converting to uint64_t/SubsecondTime

   /* Initilize DRAMsim3 simulator */
   m_dramsim = (DRAMsimCntlr**)malloc(sizeof(DRAMsimCntlr*) * m_dramsim_channels);
   for (UInt32 ch_id = 0; ch_id < m_dramsim_channels; ch_id++){
      m_dramsim[ch_id] = new DRAMsimCntlr(core_id, ch_id, m_dram_access_cost, dram_type);
   }

   if(Sim()->getCfg()->getBool(m_config_prefix + "queue_model/enabled")){
      m_bp_factor = Sim()->getCfg()->getFloat(m_config_prefix + "queue_model/bp_factor");
      ComponentPeriod dram_period = m_dramsim[0]->getDramPeriod();
      m_burst_processing_time = dram_period.getPeriod() / m_dramsim_channels / 2 / m_dramsim[0]->getLinkNumber(); // devide by 2 for DDR, divide by number of channels for parallel channels
      m_dram_burst_size = m_dramsim[0]->getBurstSize();  // in bytes
      m_backpressure_queue = QueueModel::create(
         dram_type == DramType::SYSTEM_DRAM ? "dram-queue" : "cxl-dram-queue", 
         core_id,
         Sim()->getCfg()->getString(m_config_prefix + "queue_model/type"),
         m_burst_processing_time);
   
   // ignore backward pressure unless exceeds DramSim Trans buffer. 
      m_dramsim_buffering_delay = m_dramsim[0]->getDramQueueSize() * m_burst_processing_time * (m_cache_block_size / m_dram_burst_size);
   }

   m_dram_request_size = m_dramsim[0]->getDramBlockSize();  // bytes

   if (dram_type == DramType::SYSTEM_DRAM) {
      registerStatsMetric("dram", core_id, "total-bytes-accessed", &m_total_bytes_accessed);
      registerStatsMetric("dram", core_id, "total-access-latency", &m_total_access_latency);
      registerStatsMetric("dram", core_id, "total-backpressure-latency", &m_total_bp_latency);
      registerStatsMetric("dram", core_id, "total-read-latency", &m_total_read_latency);
   } else if (dram_type == DramType::CXL_MEMORY || dram_type == DramType::CXL_VN){
      registerStatsMetric("cxl-dram", core_id, "reads", &m_reads);
      registerStatsMetric("cxl-dram", core_id, "writes", &m_writes);
      registerStatsMetric("cxl-dram", core_id, "total-bytes-accessed", &m_total_bytes_accessed);
      registerStatsMetric("cxl-dram", core_id, "total-access-latency", &m_total_access_latency);
      registerStatsMetric("cxl-dram", core_id, "total-backpressure-latency", &m_total_bp_latency);
      registerStatsMetric("cxl-dram", core_id, "total-read-latency", &m_total_read_latency);
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

DramPerfModelDramSim::~DramPerfModelDramSim()
{
   if (m_dramsim)
   {
      for (UInt32 ch_id = 0; ch_id < m_dramsim_channels; ch_id++){
         delete m_dramsim[ch_id];
      }
      free(m_dramsim);
      m_dramsim = NULL;
   }
   if (m_backpressure_queue)
   {
      delete m_backpressure_queue;
      m_backpressure_queue = NULL;
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

   SubsecondTime bp_delay = SubsecondTime::Zero();
   if (m_backpressure_queue){
      SubsecondTime processing_time = m_burst_processing_time * m_bp_factor * (pkt_size/ m_dram_burst_size);
      SubsecondTime queueing_delay = m_backpressure_queue->computeQueueDelay(
          pkt_time, processing_time, requester);
      bp_delay = queueing_delay > m_dramsim_buffering_delay ? queueing_delay - m_dramsim_buffering_delay : SubsecondTime::Zero(); 
      // ignore backward pressure if dram buffer is able to handle. 
   }

   SubsecondTime dramsim_latency = SubsecondTime::Zero();
   int num_of_dram_req = (pkt_size + m_dram_request_size - 1 ) / m_dram_request_size; // Round up number of requests
   for (int i = 0; i < num_of_dram_req; i++){
      IntPtr req_address = address + i * m_dram_request_size;
      uint32_t dramsim_ch_id = (req_address / m_dram_request_size) % m_dramsim_channels;
      IntPtr ch_addr = (req_address/m_dram_request_size) / m_dramsim_channels * m_dram_request_size;
      // fprintf(stderr, "dram_perf_model_dramsim.cc: addr 0x%lx ch %d ch_draam %lx dram req %d/%d\n", req_address, dramsim_ch_id, ch_addr, i+1, num_of_dram_req);

      SubsecondTime burst_latency = m_dramsim[dramsim_ch_id]->addTrans(pkt_time + bp_delay, ch_addr, access_type == DramCntlrInterface::WRITE);
      dramsim_latency = burst_latency > dramsim_latency ? burst_latency : dramsim_latency;
      m_total_bytes_accessed += m_dram_request_size;
   }

   SubsecondTime access_latency = dramsim_latency + bp_delay;

   switch(access_type){
      case DramCntlrInterface::READ:
         MYTRACE("0x%016lx\tREAD\t%lu", address, dramsim_latency.getNS());
         m_reads++;
         break;
      case DramCntlrInterface::WRITE:
         MYTRACE("0x%016lx\tWRITE\t%lu", address, dramsim_latency.getNS());
         m_writes++;
         break;
      default:
         LOG_ASSERT_ERROR(false, "Unsupported DramCntlrInterface::access_t(%u)", access_type);
   }
   
   perf->updateTime(pkt_time);
   perf->updateTime(pkt_time + dramsim_latency, ShmemPerf::DRAM_DEVICE);

   // Update Memory Counters only for reads
   m_num_accesses ++;
   m_total_access_latency += access_latency;
   m_total_bp_latency += bp_delay;
   if (access_type == DramCntlrInterface::READ)
      m_total_read_latency += access_latency;

   return access_latency;
}
void DramPerfModelDramSim::dramsimAdvance(SubsecondTime barrier_time){
   float new_bp_factor = 0;
   for (UInt32 ch_id = 0; ch_id < m_dramsim_channels; ch_id++){
      new_bp_factor = std::max(new_bp_factor, m_dramsim[ch_id]->advance(barrier_time));
   }
   m_bp_factor *= new_bp_factor*0.5 + 0.5;
   m_bp_factor = std::max(m_bp_factor, 1.1f);
   m_bp_factor = std::min(m_bp_factor, 10.0f);

   if ((m_bp_factor > 3 ) && new_bp_factor != 1.0) {
       fprintf(
           stderr,
           "bp factor adjust %f final m_bp_fact %f, avg lat. %lu ns\n",
           new_bp_factor, m_bp_factor,(m_total_access_latency/m_num_accesses).getNS());
   }
}

void DramPerfModelDramSim::dramsimStart(InstMode::inst_mode_t sim_status){
   Sim()->getHooksManager()->registerHook(HookType::HOOK_PERIODIC, DramPerfModelDramSim::Periodic_HOOK, (UInt64)this);
   for (UInt32 ch_id = 0; ch_id < m_dramsim_channels; ch_id++){
      m_dramsim[ch_id]->start(sim_status);
   }
}

void DramPerfModelDramSim::dramsimEnd(){
   for (UInt32 ch_id = 0; ch_id < m_dramsim_channels; ch_id++){
      m_dramsim[ch_id]->stop();
   }  
}

