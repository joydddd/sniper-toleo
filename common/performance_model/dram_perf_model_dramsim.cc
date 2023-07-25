#include "dram_perf_model_dramsim.h"
#include "hooks_manager.h"
#include "simulator.h"
#include "config.h"
#include "config.hpp"
#include "stats.h"
#include "shmem_perf.h"
#include "shmem_msg.h"
#include "memory_manager.h"

#if 1
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
   Sim()->getHooksManager()->registerHook(HookType::HOOK_ROI_BEGIN, DramPerfModelDramSim::ROIstartHOOK, (UInt64)this);
   Sim()->getHooksManager()->registerHook(HookType::HOOK_ROI_END, DramPerfModelDramSim::ROIendHOOK, (UInt64)this);

   
   m_dram_access_cost = SubsecondTime::FS() * static_cast<uint64_t>(TimeConverter<float>::NStoFS(Sim()->getCfg()->getFloat("perf_model/dram/latency"))); // Operate in fs for higher precision before converting to uint64_t/SubsecondTime

   if (Sim()->getCfg()->getBool("perf_model/dram/queue_model/enabled"))
   {
      m_queue_model = QueueModel::create("dram-queue", core_id, Sim()->getCfg()->getString("perf_model/dram/queue_model/type"),
                                         m_dram_bandwidth.getRoundedLatency(8 * cache_block_size)); // bytes to bits
   }

   /* Initilize DRAMsim3 simulator */
   m_dramsim = (DRAMsimCntlr**)malloc(sizeof(DRAMsimCntlr*) * m_dramsim_channels);
   for (UInt32 ch_id = 0; ch_id < m_dramsim_channels; ch_id++){
      m_dramsim[ch_id] = new DRAMsimCntlr(this, ch_id);
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

void DramPerfModelDramSim::dramsimStart(){
   for (UInt32 ch_id = 0; ch_id < m_dramsim_channels; ch_id++){
      m_dramsim[ch_id]->start();
   }
}

void DramPerfModelDramSim::dramsimEnd(){
   for (UInt32 ch_id = 0; ch_id < m_dramsim_channels; ch_id++){
      m_dramsim[ch_id]->stop();
   }  
}


DRAMsimCntlr::DRAMsimCntlr(DramPerfModelDramSim* perf_model, uint32_t _ch_id):
   epoch_size(Sim()->getCfg()->getInt("perf_model/dram/dramsim/epoch")),
   dram_period(ComponentPeriod::fromFreqHz(Sim()->getCfg()->getFloat("perf_model/dram/dramsim/frequency") * 1000000)), // MHz to Hz
   ch_id(_ch_id),
   status_(DRAMSIM_IDEL),
   mem_system_(NULL),
   clk_(0),
   t_start_(SubsecondTime::Zero()),
   t_latest_req_(SubsecondTime::Zero()),
   clk_latest_req_(0),
   perf_model_(perf_model)
   
{
   std::string config(Sim()->getCfg()->getString("perf_model/dram/dramsim/config").c_str());
   std::string output_dir(Sim()->getCfg()->getString("perf_model/dram/dramsim/output_dir").c_str());
   output_dir.append("/dram_cntlr_" + std::to_string(perf_model->m_core_id));
   mem_system_ = new dramsim3::MemorySystem(
       config, output_dir,
       std::bind(&DRAMsimCntlr::ReadCallBack, this, std::placeholders::_1),
       std::bind(&DRAMsimCntlr::WriteCallBack, this, std::placeholders::_1));
   
}


DRAMsimCntlr::~DRAMsimCntlr(){
   delete mem_system_;
}

void DRAMsimCntlr::runDRAMsim(uint64_t target_cycle){ // run DRAMsim until cycle count
   // run DRAMsim until 
   auto it = pending_reqs_.begin();
   for (; clk_ < target_cycle; clk_++) {
      if (it == pending_reqs_.end()) {
         status_ = DRAMSIM_DONE;
         return;
      }
      mem_system_->ClockTick();
      if (it->first <= clk_){ // req cycle <= current clk
         bool get_next = mem_system_->WillAcceptTransaction(it->second.addr, it->second.is_write);
         if (get_next) {
            fprintf(stderr, "[DRAMSIM #%d] @0x%016lx send Req %ld [%ld]cycle \n", ch_id, it->second.addr,  it->first, clk_);
            mem_system_->AddTransaction(it->second.addr, it->second.is_write);
            it = pending_reqs_.erase(it);
         }
      }
   }
}

void DRAMsimCntlr::ReadCallBack(uint64_t addr){
   fprintf(stderr, "[DRAMSIM #%d] R callback @ %016lx clk %lu\n", ch_id, addr, clk_);
}

void DRAMsimCntlr::WriteCallBack(uint64_t addr) {
   fprintf(stderr, "[DRAMSIM #%d] W callback @ %016lx clk %lu\b", ch_id, addr, clk_);
}

void DRAMsimCntlr::addTrans(SubsecondTime t_sniper, IntPtr addr, bool is_write){
   if (status_ == DRAMSIM_IDEL || status_ == DRAMSIM_DONE){
      return;
   }

   // set t_start_ if this is the first transaction
   if (status_ == DRAMSIM_AWAITING){
      LOG_ASSERT_ERROR(clk_ == 0, "clk_ %lu", clk_);
      t_start_ = t_sniper > epoch_size * dram_period.getPeriod() ? t_sniper - epoch_size * dram_period.getPeriod() : SubsecondTime::Zero(); 
      // t_start = first_req_time - epoch_size
      status_ = DRAMSIM_RUNNING;
      fprintf(stderr, "[DRAMSIM #%d] First Req at %ld ns, t0 = %ld ns clk %lu\n", ch_id, t_sniper.getNS(), t_start_.getNS(), clk_);
   }
   // DRAMSIM_RUNNING
   LOG_ASSERT_ERROR(t_sniper > t_start_, "t_sniper %ld, t_start %ld", t_sniper.getNS(), t_start_.getNS());
   uint64_t req_clk =  (t_sniper - t_start_).getInternalDataForced() / dram_period.getPeriod().getInternalDataForced();
   t_latest_req_ = t_sniper > t_latest_req_ ? t_sniper : t_latest_req_;
   clk_latest_req_ = req_clk > clk_latest_req_ ? req_clk : clk_latest_req_;
   fprintf(stderr, "[DRAMSIM #%d] @0x%016lx Add Req %ld cycle t %ld \n", ch_id, addr, req_clk, t_sniper.getNS());
   dramsim3::Transaction trans(addr, is_write);
   pending_reqs_.insert(std::pair<uint64_t, dramsim3::Transaction>(req_clk, trans));
   
   runDRAMsim(clk_latest_req_ - epoch_size);
}

void DRAMsimCntlr::start(){
   LOG_ASSERT_ERROR(status_ == DRAMSIM_IDEL, "DRAMSim3 has already been started %d", status_);
   clk_ = 0;
   status_ = DRAMSIM_AWAITING;
   fprintf(stderr, "[DRAMSIM #%d] Start DRAMsim3 at %ld ns, clk %lu\n", ch_id, t_start_.getNS(), clk_);
}

void DRAMsimCntlr::stop(){
   LOG_ASSERT_ERROR(status_ == DRAMSIM_RUNNING, "DRAMSim3 is not running %d", status_);
   // Finish pending dram requests. 
   while(status_ != DRAMSIM_DONE){
      runDRAMsim(clk_ + epoch_size);
   }
   fprintf(stderr, "[DRAMSIM #%d] Finish DRAMsim3. Latest Request %ld ns @cycel %ld, clk %lu\n", ch_id, t_latest_req_.getNS(), clk_latest_req_, clk_);
   mem_system_->PrintStats();
}