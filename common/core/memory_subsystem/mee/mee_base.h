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

class MEEBase {
    protected:
     MemoryManagerBase* m_memory_manager;
     ShmemPerfModel* m_shmem_perf_model;
     
     UInt64 m_num_encry, m_num_decry;
     UInt64 m_mac_gen, m_mac_verify;
     UInt32 m_vn_length, m_mac_length; // in bits
     UInt32 m_cache_block_size; // in bytes
     core_id_t m_core_id;

    public:
    typedef enum {
        ENCRYPT,
        DECRYPT,
        GEN_MAC,
        NUM_MEE_OP_TYPES
     } MEE_op_t;

    MEEBase(MemoryManagerBase* memory_manager, ShmemPerfModel* shmem_perf_model, core_id_t core_id, UInt32 cache_block_size);
    virtual ~MEEBase() {}

    MemoryManagerBase* getMemoryManager() { return m_memory_manager; }
    ShmemPerfModel* getShmemPerfModel() { return m_shmem_perf_model; }

    /* during WRITE: generate MAC from cipher. Write MAC to mee cache. */
    virtual boost::tuple<SubsecondTime, HitWhere::where_t> genMAC(
IntPtr address, core_id_t requester, SubsecondTime now) = 0;

    /* during READ: generate MAC from cipher. verify against MAC in cache or on DRAM/CXL expander */
    virtual boost::tuple<SubsecondTime, HitWhere::where_t> verifyMAC(
        IntPtr address, core_id_t requester, SubsecondTime now,
        ShmemPerf *perf) = 0;

    virtual SubsecondTime encryptData(IntPtr address, core_id_t requester,
                                      SubsecondTime now) = 0;
    virtual SubsecondTime decryptData(IntPtr address, core_id_t requester,
                                      SubsecondTime now, ShmemPerf *perf) = 0;

    UInt32 getVNLength() { return m_vn_length; } // in bits
    UInt32 getCacheBlockSize() { return m_cache_block_size; }
    UInt32 getMACLength() { return m_mac_length; } // in bits
    IntPtr getMACaddr(IntPtr addr);
    virtual void enablePerfModel() = 0;
    virtual void disablePerfModel() = 0;
};

#endif /* __MEE_BASE_H__ */