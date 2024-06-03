#include "dram_invisimem_cntlr.h"
#include "mee_perf_model.h"
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
   fprintf(f_trace, __VA_ARGS__); fprintf(f_trace, "\n"); fflush(f_trace); }}
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


DramInvisiMemCntlr::DramInvisiMemCntlr(MemoryManagerBase* memory_manager,
      ShmemPerfModel* shmem_perf_model,
      UInt32 cache_block_size,
      CXLAddressTranslator* cxl_address_translator)
    : DramCntlrInterface(memory_manager, shmem_perf_model, cache_block_size, cxl_address_translator),
    m_read_req_size(Sim()->getCfg()->getInt("perf_model/mee/r_req_pkt_size")),
    m_read_res_size(Sim()->getCfg()->getInt("perf_model/mee/r_res_pkt_size")),
    m_write_req_size(Sim()->getCfg()->getInt("perf_model/mee/w_req_pkt_size")),
    m_write_res_size(Sim()->getCfg()->getInt("perf_model/mee/w_res_pkt_size")),
    m_metadata_per_cl(cache_block_size / Sim()->getCfg()->getInt("perf_model/mee/mac_per_cl"))
    , m_reads(0)
    , m_writes(0)
    , m_hmc_reads(0)
    , m_hmc_writes(0)
    , m_total_data_read_delay(SubsecondTime::Zero())
    , m_total_mac_delay(SubsecondTime::Zero())
    , m_total_dram_delay(SubsecondTime::Zero())
    , m_total_decrypt_delay(SubsecondTime::Zero())
    , f_trace(NULL)
{
    UInt64 m_hmc_block_size = Sim()->getCfg()->getInt("perf_model/dram/block_size");

    m_ddr_perf_model = DramPerfModel::createDramPerfModel(
            memory_manager->getCore()->getId(), cache_block_size,
            DramType::DDR_ONLY);
    m_hmc_perf_model = DramPerfModel::createDramPerfModel(
            memory_manager->getCore()->getId(), m_hmc_block_size);
    m_mee_perf_model = new MEEPerfModel(memory_manager->getCore()->getId());
    
    // stats
    registerStatsMetric("dram", memory_manager->getCore()->getId(), "data-reads", &m_reads);
    registerStatsMetric("dram", memory_manager->getCore()->getId(), "data-writes", &m_writes);
    registerStatsMetric("dram", memory_manager->getCore()->getId(), "mac-reads", &m_reads);
    registerStatsMetric("dram", memory_manager->getCore()->getId(), "mac-writes", &m_writes);
    registerStatsMetric("dram", memory_manager->getCore()->getId(), "total-data-read-delay", &m_total_data_read_delay);
    // DEBUG:
    registerStatsMetric("dram", memory_manager->getCore()->getId(), "total-mac-delay", &m_total_mac_delay);
    registerStatsMetric("dram", memory_manager->getCore()->getId(), "total-dram-delay", &m_total_dram_delay);
    registerStatsMetric("dram", memory_manager->getCore()->getId(), "total-decrypt-delay", &m_total_decrypt_delay);


    registerStatsMetric("dram", memory_manager->getCore()->getId(), "reads", &m_hmc_reads);
    registerStatsMetric("dram", memory_manager->getCore()->getId(), "writes", &m_hmc_writes);

#ifdef MYLOG_ENABLED
    std::ostringstream trace_filename;
    trace_filename << "dram_cntlr_" << memory_manager->getCore()->getId()
                    << ".trace";
    f_trace = fopen(trace_filename.str().c_str(), "w+");
    std::cerr << "Create Dram cntlr trace " << trace_filename.str().c_str() << std::endl;
#endif // MYLOG_ENABLED
}

DramInvisiMemCntlr::~DramInvisiMemCntlr()
{
   delete m_ddr_perf_model;
   delete m_hmc_perf_model;
#ifdef MYLOG_ENABLED
   fclose(f_trace);
#endif // MYLOG_ENABLED
}

boost::tuple<SubsecondTime, HitWhere::where_t>
DramInvisiMemCntlr::getDataFromDram(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now, ShmemPerf *perf, bool is_virtual_addr)
{
    SubsecondTime ddr_access_latency = runDDRPerfModel(requester, now, address, READ, perf, is_virtual_addr);
    SubsecondTime hmc_access_latency = runHMCPerfModel(requester, now + ddr_access_latency, address, READ, perf, is_virtual_addr);
    SubsecondTime dram_access_latency = ddr_access_latency + hmc_access_latency;
    SubsecondTime aes_latency = m_mee_perf_model->getAESLatency(now + dram_access_latency, requester, MEEBase::DECRYPT, perf);
    dram_access_latency += aes_latency;

    m_total_data_read_delay += dram_access_latency;
    m_total_decrypt_delay += aes_latency;
    ++m_reads;
    MYLOG("[%d]R @ %016lx latency %s", requester, address, itostr(dram_access_latency.getNS()).c_str());
    TRACE(requester, address, DramCntlrInterface::READ);

    return boost::tuple<SubsecondTime, HitWhere::where_t>(dram_access_latency, HitWhere::DRAM);
}

boost::tuple<SubsecondTime, HitWhere::where_t>
DramInvisiMemCntlr::putDataToDram(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now, bool is_virtual_addr)
{
    SubsecondTime aes_latency = m_mee_perf_model->getAESLatency(now, requester, MEEBase::ENCRYPT, &m_dummy_shmem_perf);
    SubsecondTime ddr_access_latency = runDDRPerfModel(requester, now + aes_latency, address, WRITE, &m_dummy_shmem_perf, is_virtual_addr);
    SubsecondTime hmc_access_latency = runHMCPerfModel(requester, now + ddr_access_latency + aes_latency, address, WRITE, &m_dummy_shmem_perf, is_virtual_addr);
    SubsecondTime dram_access_latency = ddr_access_latency + hmc_access_latency;

    ++m_writes;
    MYLOG("[%d]W @ %016lx", requester, address);
    TRACE(requester, address, DramCntlrInterface::WRITE);

    return boost::tuple<SubsecondTime, HitWhere::where_t>(dram_access_latency + aes_latency, HitWhere::DRAM);
}

SubsecondTime
DramInvisiMemCntlr::runHMCPerfModel(core_id_t requester, SubsecondTime time, IntPtr address, DramCntlrInterface::access_t access_type, ShmemPerf *perf, bool is_virtual_addr)
{
    UInt64 pkt_size = getCacheBlockSize();
    IntPtr phy_addr = is_virtual_addr && m_address_translator
                            ? m_address_translator->getPhyAddress(address)
                            : address;
    IntPtr metadata_addr = m_address_translator->getMACAddrFromPhysical(phy_addr, HOST_CXL_ID);
    SubsecondTime hmc_latency; 
    SubsecondTime data_access_latency = m_hmc_perf_model->getAccessLatency(
        time, pkt_size, requester, phy_addr, access_type, perf);
    SubsecondTime metadata_access_latency = m_hmc_perf_model->getAccessLatency(
       time, m_metadata_per_cl, requester, metadata_addr, access_type, perf);
    switch (access_type) {
        case READ:
            m_hmc_reads += 2;
            m_total_dram_delay += data_access_latency;
            break;
        case WRITE:
            m_hmc_writes += 2;
            break;
        default:
            LOG_PRINT_ERROR("Unknown access type %d", access_type);
            break;
    }
    if (metadata_access_latency > data_access_latency) {
        hmc_latency = metadata_access_latency;
        if (access_type == READ) m_total_mac_delay += hmc_latency - data_access_latency;
    } else {
        hmc_latency = data_access_latency;
    }
   return hmc_latency;
}


SubsecondTime
DramInvisiMemCntlr::runDDRPerfModel(core_id_t requester, SubsecondTime time, IntPtr address, DramCntlrInterface::access_t access_type, ShmemPerf *perf, bool is_virtual_addr)
{
   UInt64 pkt_size = access_type == READ ? m_read_req_size + m_read_res_size : m_write_req_size + m_write_res_size;
   IntPtr phy_addr = is_virtual_addr && m_address_translator
                         ? m_address_translator->getPhyAddress(address)
                         : address;
   SubsecondTime ddr_access_latency = m_ddr_perf_model->getAccessLatency(
       time, pkt_size, requester, phy_addr, access_type, perf);
   return ddr_access_latency;
}


void DramInvisiMemCntlr::enablePerfModel(){
    m_ddr_perf_model->enable();
    m_hmc_perf_model->enable();
    m_mee_perf_model->enable();
}

void DramInvisiMemCntlr::disablePerfModel(){
    m_ddr_perf_model->disable();
    m_hmc_perf_model->disable();
    m_mee_perf_model->disable();
}

