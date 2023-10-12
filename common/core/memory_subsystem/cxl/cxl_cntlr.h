#ifndef __CXL_CNTLR_H
#define __CXL_CNTLR_H

#pragma once

#include "cxl_cntlr_interface.h"
#include "shmem_msg.h"
#include "shmem_perf.h"
#include "fixed_types.h"
#include "cxl_address_translator.h"
#include "cxl_perf_model.h"
#include "memory_manager_base.h"
#include "subsecond_time.h"
#include <vector>


class CXLCntlr : public CXLCntlrInterface 
{
    private:
     std::vector<bool> m_cxl_connected; 
     UInt64* m_reads, *m_writes;
     CXLPerfModel** m_cxl_perf_models;

     FILE* f_trace;
     bool enable_trace;

    public:
     CXLCntlr(MemoryManagerBase* memory_manager, ShmemPerfModel* shmem_perf_model, UInt32 cache_block_size, CXLAddressTranslator* cxl_address_tranlator, std::vector<bool>& cxl_connected);
     ~CXLCntlr();
     boost::tuple<SubsecondTime, HitWhere::where_t> getDataFromCXL(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now, ShmemPerf *perf);
     boost::tuple<SubsecondTime, HitWhere::where_t> putDataToCXL(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now);

     SubsecondTime getVNFromCXL(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now, ShmemPerf *perf) {
         LOG_ASSERT_ERROR(false, "getVNFromCXL not implemented");
     }
     SubsecondTime updateVNToCXL(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now) {
        LOG_ASSERT_ERROR(false, "getVNFromCXL not implemented");
     }

     void enablePerfModel();
     void disablePerfModel();

     friend class CXLVNServerCntlr;
};

#endif // __CXL_CNTLR_H