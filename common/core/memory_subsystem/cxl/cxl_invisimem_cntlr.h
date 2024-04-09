#ifndef __CXL_INVISIMEM_CNTLR_H
#define __CXL_INVISIMEM_CNTLR_H

#pragma once

#include "cxl_cntlr_interface.h"
#include "shmem_msg.h"
#include "shmem_perf.h"
#include "fixed_types.h"
#include "cxl_perf_model.h"
#include "dram_perf_model.h"
#include "memory_manager_base.h"
#include "subsecond_time.h"
#include <vector>


class MEEPerfModel;

class CXLInvisiMemCntlr : public CXLCntlrInterface 
{
    private:
     UInt64 m_read_req_size, m_read_res_size; // in bytes
     UInt64 m_write_req_size, m_write_res_size;  // in bytes
     UInt64 m_metadata_per_cl; // in bytes
     UInt64 m_cxl_pkt_size, m_hmc_block_size; // in bytes
     SubsecondTime m_aes_latency; 

     std::vector<bool> m_cxl_connected; 
     UInt64* m_reads, *m_writes;
     SubsecondTime* m_total_read_latency;
     SubsecondTime* m_total_decrypt_latency;
     CXLPerfModel** m_cxl_perf_models;
     DramPerfModel** m_dram_perf_models;
     MEEPerfModel** m_mee_perf_models;

     FILE* f_trace;
     bool enable_trace;

    public:
     CXLInvisiMemCntlr(MemoryManagerBase* memory_manager, ShmemPerfModel* shmem_perf_model, UInt32 cache_block_size, CXLAddressTranslator* cxl_address_tranlator, std::vector<bool>& cxl_connected);
     ~CXLInvisiMemCntlr();
     boost::tuple<SubsecondTime, HitWhere::where_t> getDataFromCXL(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now, ShmemPerf *perf, cxl_id_t cxl_id = INVALID_CXL_ID);
     boost::tuple<SubsecondTime, HitWhere::where_t> putDataToCXL(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now, cxl_id_t cxl_id = INVALID_CXL_ID);

     SubsecondTime getVNFromCXL(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now, ShmemPerf *perf) {
         LOG_ASSERT_ERROR(false, "getVNFromCXL not implemented");
     }
     SubsecondTime updateVNToCXL(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now) {
        LOG_ASSERT_ERROR(false, "getVNFromCXL not implemented");
     }

     void enablePerfModel();
     void disablePerfModel();

};

#endif // __CXL_CNTLR_H