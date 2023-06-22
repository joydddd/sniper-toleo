#ifndef __MEE_NAIVE_H__
#define __MEE_NAIVE_H__
#include "mee_base.h"
#include "mee_perf_model.h"
#include "cache.h"

class MEENaive : public MEEBase {
    private:
     UInt32 m_mac_cache_size /* in bytes */, m_mac_per_cacheline;
     SubsecondTime m_mac_cache_tag_latency, m_mac_cache_data_latency;

     UInt64 m_mac_gen_misses, m_mac_verify_misses;
     UInt64 m_mac_dirty_evicts;

     Cache* m_mac_cache;
     MEEPerfModel *m_mme_perf_model;

     ShmemPerf m_dummy_shmem_perf;
     IntPtr getMACaddr(IntPtr address);
     IntPtr getOGaddr(IntPtr mac_addr);
     boost::tuple<SubsecondTime, HitWhere::where_t> accessMAC(
         IntPtr address, Cache::access_t access, core_id_t requester,
         SubsecondTime now, ShmemPerf *perf);

    public:
     MEENaive(core_id_t core_id, UInt32 cache_block_size);
     ~MEENaive();

    /* called at STORE to DRAM & CXL. generates MAC and write to MAC cache, return HitWhere::where_t if miss local mac cache*/
     boost::tuple<SubsecondTime, HitWhere::where_t> genMAC(
         IntPtr address, core_id_t requester, SubsecondTime now);

    /* called at LOAD to DRAM & CXL. generates MAC and verify against cached MAC, return HitWhere::where_t if miss local mac cache*/
     boost::tuple<SubsecondTime, HitWhere::where_t> verifyMAC(
         IntPtr address, core_id_t requester, SubsecondTime now, ShmemPerf *perf);

    /* called at LOAD/STORE to DRAM & CXL. insert newly fetch MAC into mac cache. return true if cacheline write back is needed. */
     void insertMAC(
         IntPtr address, bool &dirty_eviction, IntPtr &_evict_addr, Cache::access_t access, core_id_t requester, SubsecondTime now);

     SubsecondTime encryptData(IntPtr address, core_id_t requester, SubsecondTime now);
     SubsecondTime decryptData(IntPtr address, core_id_t requester, SubsecondTime now, ShmemPerf *perf);

     void enablePerfModel();
     void disablePerfModel();
};



#endif // __MEE_NAIVE_H__