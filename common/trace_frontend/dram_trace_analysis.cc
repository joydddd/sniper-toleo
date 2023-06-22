#include "dram_trace_analysis.h"
#include "simulator.h"
#include "hooks_manager.h"
#include "log.h"

#define CACHE_LINE_SIZE 64 
#define CACHE_LINE_BITS 6
#define PAGE_SIZE 4096
#define PAGE_BITS 12

#define VAULT_PRIVATE_BITS 7
#define VAULT_PRIVATE_MAX (1 << VAULT_PRIVATE_BITS) - 1

void Vault_Page::write(UInt8 cl_num){
    if (!written) {
        written = true;
        shared_vn = 0;
        for(int i = 0; i < 64; i++){
            private_vn[i] = 0;
        }
    }

    ++private_vn[cl_num];
    if (private_vn[cl_num] > VAULT_PRIVATE_MAX){
        ++shared_vn;
        for (int i = 0; i < 64; i++) private_vn[i] = 0; 
    }
}

Vault_Page::vn_comp_t Vault_Page::type(){
    if (!written) return READ_ONLY;
    if (shared_vn != 0) return OVERFLOW;
    max_private = 0, min_private = UINT8_MAX;
    for (int i = 0; i < 64; i++){
        if (private_vn[i] > max_private) max_private = private_vn[i];
        if (private_vn[i] < min_private) min_private = private_vn[i];
    } 
    
    if (max_private == min_private) return UNIFORM_WRITE;
    if (max_private - min_private == 1) return ONE_STEP;
    return VAULT;
}

DramTraceAnalyzer::DramTraceAnalyzer()
    : m_reads(0)
    , m_writes(0)
    , m_page_touched(0)
    , m_page_dirty(0)
    , m_page_readonly(0)
    , m_page_uniform_write(0)
    , m_page_one_step(0)
    , m_page_multi_write(0)
{
    Sim()->getHooksManager()->registerHook(HookType::HOOK_APPLICATION_START, DramTraceAnalyzer::ROIstartHOOK, (UInt64)this);
    Sim()->getHooksManager()->registerHook(HookType::HOOK_APPLICATION_ROI_BEGIN, DramTraceAnalyzer::ROIstartHOOK, (UInt64)this);
    Sim()->getHooksManager()->registerHook(HookType::HOOK_APPLICATION_ROI_END, DramTraceAnalyzer::ROIendHOOK, (UInt64)this);
    Sim()->getHooksManager()->registerHook(HookType::HOOK_APPLICATION_EXIT, DramTraceAnalyzer::ROIendHOOK, (UInt64)this);
    
    registerStatsMetric("dram", 0, "total-reads", &m_reads);
    registerStatsMetric("dram", 0, "total-writes", &m_writes);
    registerStatsMetric("dram", 0, "page-touched", &m_page_touched);
    registerStatsMetric("dram", 0, "page-dirty", &m_page_dirty);
    registerStatsMetric("dram", 0, "page-readonly", &m_page_readonly);
    registerStatsMetric("dram", 0, "page-uniform-write", &m_page_uniform_write);
    registerStatsMetric("dram", 0, "page-one-step", &m_page_one_step);
    registerStatsMetric("dram", 0, "page-multi-write", &m_page_multi_write);

    f_log = fopen("dram_trace_analysis.txt", "w");
}

void DramTraceAnalyzer::RecordDramAccess(core_id_t core_id, IntPtr address, DramCntlrInterface::access_t access_type){
    IntPtr page_num = address >> PAGE_BITS;
    UInt8 cl_num = (address & (PAGE_SIZE - 1)) >> CACHE_LINE_BITS;

    auto page_it = m_vault_vn.find(page_num);
    if (page_it == m_vault_vn.end()) {
        m_vault_vn[page_num] = Vault_Page();
        m_page_touched++;
    }
    switch(access_type){
        
        case DramCntlrInterface::READ:
            m_reads++;
            break;
        case DramCntlrInterface::WRITE:
            m_writes++;
            m_vault_vn[page_num].write(cl_num);
            break;
        default:
            LOG_PRINT_ERROR("Unknow access type %u", access_type);
    }
}

void DramTraceAnalyzer::AnalyzeDramAccess(){
    for (auto it = m_vault_vn.begin(); it != m_vault_vn.end(); it++){
        Vault_Page::vn_comp_t type = it->second.type();
        switch(type){
            case Vault_Page::READ_ONLY:
                m_page_readonly++;
                break;
            case Vault_Page::UNIFORM_WRITE:
                m_page_uniform_write++;
                m_page_dirty++;
                fprintf(f_log, "%lx UNIFORM WRITE %d\n", it->first, it->second.max_private);
                break;
            case Vault_Page::ONE_STEP:
                m_page_one_step++;
                m_page_dirty++;
                fprintf(f_log, "%lx ONE STEP %d - %d\n", it->first, it->second.max_private, it->second.min_private);
                break;
            case Vault_Page::VAULT:
                fprintf(f_log, "%lx VAULT %d - %d\n", it->first, it->second.max_private, it->second.min_private);
                m_page_multi_write++;
                m_page_dirty++;
                break;
            case Vault_Page::OVERFLOW:
                fprintf(f_log, "%lx OVERFLOW shared %d \n", it->first, it->second.shared_vn);
                m_page_multi_write++;
                m_page_dirty++;
               
        }
    }
    fprintf(f_log, "========================================================\n");
    fprintf(f_log, "Total Reads: %lu\n", m_reads);
    fprintf(f_log, "Total Writes: %lu\n", m_writes);
    fprintf(f_log, "Total Page Touched: %lu\n", m_page_touched);
    fprintf(f_log, "Total Page Dirty: %lu\n", m_page_dirty);
    fprintf(f_log, "Total Page Read Only: %lu\n", m_page_readonly);
    fprintf(f_log, "Total Page Uniform Write: %lu\n", m_page_uniform_write);
    fprintf(f_log, "Total Page One Step: %lu\n", m_page_one_step);
    fprintf(f_log, "Total Page Multi Write: %lu\n", m_page_multi_write);
}

void DramTraceAnalyzer::InitDramAccess(){
    fprintf(f_log, "Init Dram Access Analyzer\n");
    m_reads = 0;
    m_writes = 0;
    m_page_touched = 0;
    m_page_dirty = 0;
    m_page_readonly = 0;
    m_page_uniform_write = 0;
    m_page_one_step = 0;
    m_page_multi_write = 0;
    m_vault_vn.clear();
}