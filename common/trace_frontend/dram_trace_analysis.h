#ifndef __TRACE_ANALYSIS_H_
#define __TRACE_ANALYSIS_H_

#include "dram_cntlr.h"
#include "log.h"
#include "stats.h"

#include <unordered_map>

class Vault_Page {
    public:
    typedef enum { READ_ONLY = 0, UNIFORM_WRITE, ONE_STEP, VAULT, OVERFLOW } vn_comp_t;

    bool written = false;
    bool sparse = false; // if VN is more than 1 apart during ROI. 
    vn_comp_t type = READ_ONLY;
    UInt32 shared_vn;
    UInt8 private_vn[64];
    UInt8 max_private, min_private;


    Vault_Page():written(false){}
    ~Vault_Page(){}

    void write(UInt8 cl_num);
    vn_comp_t gen_type();
};

class DramTraceAnalyzer {
   private:
    bool enable = false;
    UInt64 m_reads, m_writes;
    UInt64 m_page_touched, m_page_dirty;
    UInt64 m_page_readonly, m_page_uniform_write, m_page_one_step,
        m_page_multi_write;

    std::unordered_map<IntPtr, Vault_Page> m_vault_vn;

    FILE* f_log;

   public:
    DramTraceAnalyzer();
    ~DramTraceAnalyzer() {fclose(f_log);}

    void RecordDramAccess(core_id_t core_id, IntPtr address,
                          DramCntlrInterface::access_t access_type);
    void AnalyzeDramAccess();
    void InitDramAccess();
    static SInt64 ROIstartHOOK(UInt64 object, UInt64 argument){
        ((DramTraceAnalyzer*)object)->enable = true;
        ((DramTraceAnalyzer*)object)->InitDramAccess();
        return 0;
    }
    static SInt64 ROIendHOOK(UInt64 object, UInt64 argument){
        ((DramTraceAnalyzer*)object)->enable = false;
        ((DramTraceAnalyzer*)object)->AnalyzeDramAccess();
        return 0;
    }
};

#endif // __TRACE_ANALYSIS_H_