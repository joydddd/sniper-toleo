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
   m_cache_block_size(cache_block_size),
   m_total_access_latency(SubsecondTime::Zero()),
   m_total_read_latency(SubsecondTime::Zero()),
   m_dramsim(NULL),
   m_dramsim_channels(dram_type == DramType::SYSTEM_DRAM ? 
                     Sim()->getCfg()->getInt("perf_model/dram/dramsim/channles_per_contoller") :
                     dram_type == DramType::CXL_MEMORY ?
                     Sim()->getCfg()->getInt("perf_model/cxl/memory_expander_" + itostr((unsigned int)core_id) + "/dram/dramsim/channles_per_contoller") :
                     Sim()->getCfg()->getInt("perf_model/cxl/vnserver/dram/dramsim/channles_per_contoller"))
   
{
   Sim()->getHooksManager()->registerHook(HookType::HOOK_INSTRUMENT_MODE, DramPerfModelDramSim::Change_mode_HOOK, (UInt64)this);
   
   m_dram_access_cost = SubsecondTime::FS() * static_cast<uint64_t>(TimeConverter<float>::NStoFS(Sim()->getCfg()->getFloat("perf_model/dram/default_latency"))); // Operate in fs for higher precision before converting to uint64_t/SubsecondTime

   /* Initilize DRAMsim3 simulator */
   m_dramsim = (DRAMsimCntlr**)malloc(sizeof(DRAMsimCntlr*) * m_dramsim_channels);
   for (UInt32 ch_id = 0; ch_id < m_dramsim_channels; ch_id++){
      m_dramsim[ch_id] = new DRAMsimCntlr(core_id, ch_id, m_dram_access_cost, dram_type);
   }

   if (dram_type == DramType::SYSTEM_DRAM) {
      registerStatsMetric("dram", core_id, "total-access-latency", &m_total_access_latency);
      registerStatsMetric("dram", core_id, "total-read-latency", &m_total_read_latency);
   } else if (dram_type == DramType::CXL_MEMORY || dram_type == DramType::CXL_VN){
      registerStatsMetric("cxl-dram", core_id, "total-access-latency", &m_total_access_latency);
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

   SubsecondTime dramsim_latency;
   uint32_t dramsim_ch_id = (address / m_cache_block_size) % m_dramsim_channels;
   IntPtr ch_addr = (address/m_cache_block_size) / m_dramsim_channels * m_cache_block_size;
   dramsim_latency = m_dramsim[dramsim_ch_id]->addTrans(pkt_time, ch_addr, access_type == DramCntlrInterface::WRITE);

   SubsecondTime access_latency = dramsim_latency;

   switch(access_type){
      case DramCntlrInterface::READ:
         MYTRACE("0x%016lx\tREAD\t%lu", address, dramsim_latency.getNS());
         break;
      case DramCntlrInterface::WRITE:
         MYTRACE("0x%016lx\tWRITE\t%lu", address, dramsim_latency.getNS());
         break;
      default:
         LOG_ASSERT_ERROR(false, "Unsupported DramCntlrInterface::access_t(%u)", access_type);
   }
   
   perf->updateTime(pkt_time);
   perf->updateTime(pkt_time + dramsim_latency, ShmemPerf::DRAM_DEVICE);

   // Update Memory Counters only for reads
   m_num_accesses ++;
   m_total_access_latency += access_latency;
   if (access_type == DramCntlrInterface::READ)
      m_total_read_latency += access_latency;

   return access_latency;
}
void DramPerfModelDramSim::dramsimAdvance(SubsecondTime barrier_time){
   for (UInt32 ch_id = 0; ch_id < m_dramsim_channels; ch_id++){
      m_dramsim[ch_id]->advance(barrier_time);
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

