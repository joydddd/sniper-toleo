#ifndef __MEE_NAIVE_H__
#define __MEE_NAIVE_H__
#include "mee_base.h"
#include "mee_perf_model.h"

class MEENaive : public MEEBase {
    private:
     MEEPerfModel *m_mme_perf_model;

     ShmemPerf m_dummy_shmem_perf;

    public:
     MEENaive(core_id_t core_id);
     ~MEENaive();

     SubsecondTime genMAC(IntPtr address, core_id_t requester, SubsecondTime now, ShmemPerf *perf);
     /* takes both plaintext data checksum and VN as input */
     
     SubsecondTime encryptData(IntPtr address, core_id_t requester, SubsecondTime now);
     SubsecondTime decryptData(IntPtr address, core_id_t requester, SubsecondTime now, ShmemPerf *perf);

     UInt32 getVNLength() { return m_vn_length; }

     void enablePerfModel();
     void disablePerfModel();
};



#endif // __MEE_NAIVE_H__