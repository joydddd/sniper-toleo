#ifndef __MEE_NAIVE_H__
#define __MEE_NAIVE_H__
#include "mee_base.h"
#include "mee_perf_model.h"
#include "cache.h"
#include "cxl/cxl_cntlr_interface.h"
#include "dram_cntlr_interface.h"

class MEENaive : public MEEBase {
    private:
     UInt32 m_mee_cache_size /* in bytes */, m_mac_per_cacheline;
     SubsecondTime m_mee_cache_tag_latency, m_mee_cache_data_latency;

     UInt64 m_mac_gen_misses, m_mac_fetch_misses;

     Cache* m_mee_cache;
     MEEPerfModel *m_mme_perf_model;
     
     CXLCntlrInterface* m_cxl_cntlr;
     DramCntlrInterface* m_dram_cntlr;

     ShmemPerf m_dummy_shmem_perf;
     FILE* f_trace;
     bool m_enable_trace;

     boost::tuple<SubsecondTime, SubsecondTime, HitWhere::where_t> accessMACVN(
         IntPtr address, Cache::access_t access, core_id_t requester,
         SubsecondTime now, ShmemPerf *perf);

     void insertMAC(Cache::access_t access, IntPtr address, core_id_t requester, SubsecondTime now);


    public:
     MEENaive(MemoryManagerBase* memory_manager, ShmemPerfModel* shmem_perf_model, core_id_t core_id, UInt32 cache_block_size, CXLCntlrInterface* m_cxl_cntlr, DramCntlrInterface *m_dram_cntlr = NULL);
     ~MEENaive();

     boost::tuple<SubsecondTime, HitWhere::where_t> genMAC(
         IntPtr address, core_id_t requester, SubsecondTime now);

     SubsecondTime verifyMAC(IntPtr address, core_id_t requester,
                             SubsecondTime now, ShmemPerf* perf);
     boost::tuple<SubsecondTime, SubsecondTime, HitWhere::where_t> fetchMACVN(
         IntPtr address, core_id_t requester, SubsecondTime now,
         ShmemPerf* perf);
     SubsecondTime encryptData(IntPtr address, core_id_t requester, SubsecondTime now);
     SubsecondTime decryptData(IntPtr address, core_id_t requester, SubsecondTime now, ShmemPerf *perf);

     void enablePerfModel();
     void disablePerfModel();
};



#endif // __MEE_NAIVE_H__