#ifndef __MEE_NAIVE_H__
#define __MEE_NAIVE_H__
#include "mee_base.h"
#include "mee_perf_model.h"
#include "cache.h"
#include "cxl/cxl_cntlr_interface.h"
#include "dram_cntlr_interface.h"

class MEENaive : public MEEBase {
    private:
     cxl_id_t m_cxl_id;

     /* MAC cache is implmented as a write ALLOCATE cache. */
     UInt32 m_mac_cache_size /* in bytes */, m_mac_per_cacheline;
     SubsecondTime m_mac_cache_tag_latency, m_mac_cache_data_latency;
     UInt64 m_mac_misses;
     Cache* m_mac_cache;

     /* VN table is cimplemented as a write through cache. updates invalidates the entry.*/
     UInt32 m_vn_table_entries, m_vn_per_entry;
     SubsecondTime m_vn_table_latency;
     UInt64 m_vn_misses;
     Cache* m_vn_table;

     MEEPerfModel *m_mee_perf_model;
     
     CXLCntlrInterface* m_cxl_cntlr;
     DramCntlrInterface* m_dram_cntlr;

     ShmemPerf m_dummy_shmem_perf;
     FILE* f_trace;
     bool m_enable_trace;

    /* MAC cache operations */
     boost::tuple<SubsecondTime, HitWhere::where_t> accessMAC(
         IntPtr v_addr, Cache::access_t access, core_id_t requester,
         SubsecondTime now, ShmemPerf *perf);

     void insertMAC(Cache::access_t access, IntPtr mac_addr, core_id_t requester, SubsecondTime now);

     /* VN cache operations */
     /* insert into VN table if entry doesn't exist. return true if new entry is allocated. */
     bool insertVN(IntPtr v_addr, core_id_t requester, SubsecondTime now);


     /* lookup VN table. return HitWhere::CXL_VN or HitWhere::MEE_CACHE, access latency
      * In case of cache miss: 
      *     for CXL MEEs, get VN locally on this CXL. 
      *     for DRAM MEEs, do nothing
      * Neither allocates the entry for the VN fetched / to be fetched. 
      */
     boost::tuple<SubsecondTime, HitWhere::where_t> lookupVN(
         IntPtr v_addr, core_id_t requester, SubsecondTime now,
         ShmemPerf* perf);

    public:
     MEENaive(MemoryManagerBase* memory_manager,
              ShmemPerfModel* shmem_perf_model,
              CXLAddressTranslator* address_transltor, core_id_t core_id,
              UInt32 cache_block_size, CXLCntlrInterface* m_cxl_cntlr,
              DramCntlrInterface* m_dram_cntlr = NULL);
     ~MEENaive();

    /* during WRITE:
    Input: 
        plaintext (512bit)
        VN (updated)
    Output:
        ciphertext
        MAC (and write to MAC cache)
    */
     SubsecondTime EncryptGenMAC(IntPtr address, core_id_t requester,
                                 SubsecondTime now);

     /* during READ:
     Input: MAC, ciphertext, VN
     Output: plaintext
     */
     SubsecondTime DecryptVerifyData(IntPtr address, core_id_t requester,
                                     SubsecondTime now, ShmemPerf* perf);
     /* during READ: fetch MAC and VN from corresponding mem ctnlr/mee cache
     Return: MAC latency, VN latency, hitwhere */
     boost::tuple<SubsecondTime, SubsecondTime, HitWhere::where_t> fetchMACVN(
         IntPtr address, core_id_t requester, SubsecondTime now,
         ShmemPerf* perf);

     void enablePerfModel();
     void disablePerfModel();
};



#endif // __MEE_NAIVE_H__