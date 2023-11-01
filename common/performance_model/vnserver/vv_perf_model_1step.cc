#include "vv_perf_model_1step.h"
#include "simulator.h"
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
    fprintf(f_trace, "[VV] ");          \
    fprintf(f_trace, __VA_ARGS__);                      \
    fprintf(f_trace, "\n");                             \
    fflush(f_trace);                                    \
}
#else
#define MYTRACE(...) {}
#endif




#define VAULT_PRIVATE_MAX 127
#define PAGE_OFFSET 12 // 4KB page (64 CLs)
#define PAGE_SIZE 4096 // 4KB page (64 CLs)
#define CACHE_LINE_OFFSET 6 // 64B cache line
#define VN_ENTRY_SIZE 16 // in bytes

VN_Page::vn_comp_t VN_Page::update(UInt8 cl_num){
    switch (type){
        case ONE_STEP:
            ++vn_private[cl_num];
            if (vn_private[cl_num] > 1) type = VAULT;
            break;
        case VAULT:
            ++vn_private[cl_num];
            if (vn_private[cl_num] >= VAULT_PRIVATE_MAX) type = OVERFLOW;
            break;
        case OVERFLOW:
            ++vn_private[cl_num];
            break;
    }
    m_total_offset++;
    if (type == ONE_STEP && m_total_offset == 64) { // rollover one step pages if they are uniform now
        m_total_offset = 0;
        for (int i = 0; i < 64; i++) vn_private[i] = 0;
    }
    return type;
}

UInt32 VN_Page::reset(){
    UInt32 ret = m_total_offset;
    
    if (type == ONE_STEP) {
        ret = m_total_offset == 0 ? 0 : 64 - m_total_offset; // pages that are not written to need a forced update
    }
    if (type == VAULT || type == OVERFLOW) {
        ret = 64;
        return 64;
    }

    m_total_offset = 0;
    type = ONE_STEP;
    for (int i = 0; i < 64; i++) vn_private[i] = 0;
    return ret;
}




VVPerfModel1Step::VVPerfModel1Step(cxl_id_t cxl_id, UInt32 vn_size)
    : VVPerfModel(vn_size),
    m_dram_perf_model(NULL),
    m_total_read_delay(SubsecondTime::Zero()),
    m_total_update_latency(SubsecondTime::Zero())
    {
    fprintf(stderr, "Create Dram perf model from vvperfmodel, cxl_id %d\n", cxl_id);
    m_dram_perf_model = DramPerfModel::createDramPerfModel(cxl_id, VN_ENTRY_SIZE, DramType::CXL_VN);

    registerStatsMetric("vv", cxl_id, "total-read-delay", &m_total_read_delay);
    registerStatsMetric("vv", cxl_id, "total-update-latency", &m_total_update_latency);
    registerStatsMetric("vv", cxl_id, "dram-reads", &m_dram_reads);
    registerStatsMetric("vv", cxl_id, "dram-writes", &m_dram_writes);

#ifdef MYTRACE_ENABLED
   f_trace = fopen("vv_perf.trace", "w+");
   std::cerr << "Create Version Vault perf trace " << "vv_perf.trace" << std::endl;
#endif // MYTRACE_ENABLED
}

VVPerfModel1Step::~VVPerfModel1Step()
{
    if (m_dram_perf_model)
    {
        delete m_dram_perf_model;
        m_dram_perf_model = NULL;
    }
}

// return latency, pkt_size (in bits)
boost::tuple<SubsecondTime, UInt64> VVPerfModel1Step::getAccessLatency(
    SubsecondTime pkt_time, core_id_t requester, IntPtr address,
    CXLCntlrInterface::access_t access_type, ShmemPerf* perf) 
{
    // pkt_size is in 'bits'
    if ((!m_enabled) || (requester >= (core_id_t) Config::getSingleton()->getApplicationCores()))
        return boost::make_tuple<SubsecondTime, UInt64>(SubsecondTime::Zero(),  VN_ENTRY_SIZE);


    // get Vault Page Type
    VN_Page::vn_comp_t vn_page_type = VN_Page::vn_comp_t::ONE_STEP;
    IntPtr page_num = address >> PAGE_OFFSET;
    UInt8 cl_num = (address & (PAGE_SIZE - 1)) >> CACHE_LINE_OFFSET;
    auto page_it = m_vault_pages.find(page_num);
    if (page_it != m_vault_pages.end()) { // if page exists. 
       vn_page_type = page_it->second.get_type();
    } 

    // read pointer if this is not a one-step page
    SubsecondTime dram_latency = SubsecondTime::Zero();
    if (vn_page_type != VN_Page::vn_comp_t::ONE_STEP){ // get entry address if not one-step page
        dram_latency += m_dram_perf_model->getAccessLatency(pkt_time, VN_ENTRY_SIZE, requester, address, DramCntlrInterface::READ, perf);
        m_dram_reads++;
    }

    // read the whole entry. Each transaction reads one VN_ENTRY_SIZE
    SubsecondTime dram_latency_max = SubsecondTime::Zero(); // max latency of all transactions
    for (int size_read = 0; size_read < (UInt64)vn_page_type; size_read += VN_ENTRY_SIZE){
        SubsecondTime dram_lat_trans = m_dram_perf_model->getAccessLatency(
            pkt_time + dram_latency, VN_ENTRY_SIZE, requester, address,
            DramCntlrInterface::READ, perf);
        dram_latency_max = dram_latency_max > dram_lat_trans ? dram_latency_max : dram_lat_trans;
    }
    dram_latency += dram_latency_max;
    m_dram_reads++;
    // update only the dirty entry if neccessary 
    if (access_type == CXLCntlrInterface::VN_UPDATE) {
        // if page doesn't exist, create it
        if (page_it == m_vault_pages.end())
            page_it = m_vault_pages.insert(std::make_pair(page_num, VN_Page())).first;

        // write after read for vn updates
        m_dram_perf_model->getAccessLatency(pkt_time + dram_latency, VN_ENTRY_SIZE, requester, address, DramCntlrInterface::WRITE, perf);
        m_dram_writes++;

        // update the page
        VN_Page::vn_comp_t new_page_type = page_it->second.update(cl_num);
        if (new_page_type != vn_page_type){ // if we are allocating new entry for this page
            m_vault_pages[page_num] = VN_Page();
            vn_page_type = new_page_type;
            for (int size_read = 0; size_read < (UInt64)vn_page_type; size_read += VN_ENTRY_SIZE)
                m_dram_perf_model->getAccessLatency(pkt_time + dram_latency, VN_ENTRY_SIZE, requester, address, DramCntlrInterface::WRITE, perf);
        }
    }

    SubsecondTime access_latency = dram_latency;
    switch(access_type){
        case CXLCntlrInterface::VN_READ:
            MYTRACE("0x%016lx\tREAD\t%lu\t%lu", address, access_latency.getNS(), (UInt64)vn_page_type);
            m_total_read_delay += access_latency;
            m_num_reads++;
            break;
        case CXLCntlrInterface::VN_UPDATE:
            MYTRACE("0x%016lx\tUPDATE\t%lu\t%lu", address, access_latency.getNS(), (UInt64)vn_page_type);
            m_total_update_latency += access_latency;
            m_num_updates++;
            break;
        default:
            LOG_ASSERT_ERROR(false, "Unsupported access type %d on VV", access_type);
            break;
    }
    return boost::make_tuple<SubsecondTime, UInt64>(access_latency, (UInt64)vn_page_type);
}

void VVPerfModel1Step::enable(){
    m_enabled = true;
    m_dram_perf_model->enable();
}

void VVPerfModel1Step::disable(){
    m_enabled = false;
    m_dram_perf_model->disable();
}
