#ifndef __TRACE_ANALYSIS_H_
#define __TRACE_ANALYSIS_H_

#include "dram_cntlr.h"
#include "log.h"
#include "stats.h"

#include <unordered_map>

class Vault_Page {
    public:
    typedef enum { READ_ONLY = 0, ONE_WRITE, ONE_STEP, VAULT, OVER_FLOW } vn_comp_t;

    bool written = false;
    bool sparse = false; // if VN is more than 1 apart during ROI.
    bool epoch = false;
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
    UInt64 m_page_readonly, m_page_one_write, m_page_one_step,
        m_page_multi_write, m_sparse;

    UInt64 m_ins_count_next = 0, m_ins_count_epoch_next = 0;

    std::unordered_map<IntPtr, Vault_Page> m_vault_vn;

    FILE *f_log, *f_csv;

    void GarbageCollection();
    void EndEpoch();

   public:
    DramTraceAnalyzer();
    ~DramTraceAnalyzer() {fclose(f_log);}

    void RecordDramAccess(core_id_t core_id, IntPtr address,
                          DramCntlrInterface::access_t access_type);
    void AnalyzeDramAccess();
    void InitDramAccess();
    void periodic(UInt64 icount);
    static SInt64 ROIstartHOOK(UInt64 object, UInt64 argument) {
        ((DramTraceAnalyzer*)object)->enable = true;
        ((DramTraceAnalyzer*)object)->InitDramAccess();
        return 0;
    }
    static SInt64 ROIendHOOK(UInt64 object, UInt64 argument){
        ((DramTraceAnalyzer*)object)->enable = false;
        ((DramTraceAnalyzer*)object)->AnalyzeDramAccess();
        return 0;
    }
    static SInt64 PeriodicHOOK(UInt64 object, UInt64 argument){
        ((DramTraceAnalyzer*)object)->periodic(argument);
        return 0;
    }
};

#endif // __TRACE_ANALYSIS_H_