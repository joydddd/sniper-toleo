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

class MEEBase {
    protected:
     UInt64 m_num_encry, m_num_decry;
     UInt64 m_mac_gen;
     UInt32 m_vn_length;
     core_id_t m_core_id;

    public:
    typedef enum {
        ENCRYPT,
        DECRYPT,
        GEN_MAC,
        NUM_MEE_OP_TYPES
     } MEE_op_t;

    MEEBase(core_id_t core_id);
    virtual ~MEEBase() {}

    virtual SubsecondTime genMAC(IntPtr address, core_id_t requester,
                                 SubsecondTime now, ShmemPerf *perf) = 0;
    virtual SubsecondTime encryptData(IntPtr address, core_id_t requester,
                                      SubsecondTime now) = 0;
    virtual SubsecondTime decryptData(IntPtr address, core_id_t requester,
                                      SubsecondTime now, ShmemPerf *perf) = 0;

    UInt32 getVNLength() { return m_vn_length; }
    virtual void enablePerfModel() = 0;
    virtual void disablePerfModel() = 0;
};

#endif /* __MEE_BASE_H__ */