#ifndef __MEE_BASE_H__
#define __MEE_BASE_H__

#include "subsecond_time.h"
#include "fixed_types.h"
#include "shmem_msg.h"
#include "shmem_perf.h"
#include "boost/tuple/tuple.hpp"
#include "simulator.h"
#include "config.hpp"
#include "config.h"

class MemoryManagerBase;
class ShmemPerfModel;
class ShmemPerf;
class CXLAddressTranslator;

class MEEBase {
    protected:
     bool m_mac_cache_enabled, m_vn_cache_enabled;
     MemoryManagerBase* m_memory_manager;
     ShmemPerfModel* m_shmem_perf_model;
     CXLAddressTranslator* m_address_translator;

     UInt64 m_encry_macs, m_decry_verifys;
     UInt64 m_mac_reads;
     UInt32 m_vn_length;        // in bits
     UInt32 m_cache_block_size; // in bytes
     core_id_t m_core_id;

    public:
    typedef enum {
        ENCRYPT,
        DECRYPT,
        GEN_MAC,
        NUM_MEE_OP_TYPES
     } MEE_op_t;

    MEEBase(MemoryManagerBase* memory_manager, ShmemPerfModel* shmem_perf_model,
            CXLAddressTranslator* address_translator, core_id_t core_id, // If MEE is used for a CXL cntlr, core_id = cxl_id + 1
            UInt32 cache_block_size);
    virtual ~MEEBase() {}

    MemoryManagerBase* getMemoryManager() { return m_memory_manager; }
    ShmemPerfModel* getShmemPerfModel() { return m_shmem_perf_model; }

    /* during WRITE:
    Input: 
        plaintext (512bit)
        VN (updated)
    Output:
        ciphertext
        MAC (and write to MAC cache)
    */
    virtual SubsecondTime EncryptGenMAC(IntPtr address, core_id_t requester,
                                        SubsecondTime now) = 0;

    /* during READ: 
    Input:
        MAC
        ciphertext
        VN
    Output:
        plaintext
    */
    virtual SubsecondTime DecryptVerifyData(IntPtr address, core_id_t requester,
                                            SubsecondTime now,
                                            ShmemPerf* perf) = 0;
    /* during READ: fetch MAC and VN from corresponding mem ctnlr/mee cache
    Return: MAC latency, VN latency, hitwhere */
    virtual boost::tuple<SubsecondTime, SubsecondTime, HitWhere::where_t>
    fetchMACVN(IntPtr address, core_id_t requester, SubsecondTime now,
               ShmemPerf* perf) = 0;

    UInt32 getVNLength() { return m_vn_length; } // in bits
    UInt32 getCacheBlockSize() { return m_cache_block_size; }
    virtual void enablePerfModel() = 0;
    virtual void disablePerfModel() = 0;
};

#endif /* __MEE_BASE_H__ */