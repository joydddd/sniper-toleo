#ifndef __CXL_VN_CNTLR_H
#define __CXL_VN_CNTLR_H

#pragma once

#include "cxl_cntlr_interface.h"
#include "shmem_msg.h"
#include "shmem_perf.h"
#include "cache.h"
#include "fixed_types.h"
#include "cxl_perf_model.h"
#include "cxl_cntlr.h"
#include "mee_base.h"
#include "memory_manager_base.h"
#include "subsecond_time.h"
#include <vector>


class CXLVNServerCntlr : public CXLCntlrInterface 
{
    private:
     cxl_id_t m_vnserver_cxl_id;
     UInt64 m_vn_length, m_cxl_pkt_size;
     UInt64 m_vn_reads, m_vn_updates;
     UInt64 *m_data_reads, *m_data_writes; // cxl data reads and write
     UInt64 *m_mac_reads, *m_mac_writes; // cxl memory expander reads and writes due to mac access.
     SubsecondTime *m_total_read_delay; // cxl device total read delay for data accesses. 

     CXLPerfModel* m_vn_perf_model;
     CXLCntlr* m_cxl_cntlr;
     std::vector<MEEBase*> m_mee;

     FILE* f_trace;
     bool m_enable_trace;
     
     SubsecondTime getVN(IntPtr address, core_id_t requester, SubsecondTime now, ShmemPerf *perf);
     SubsecondTime updateVN(IntPtr address, core_id_t requester, SubsecondTime now, ShmemPerf *perf);
     SubsecondTime getMAC(IntPtr mac_addr, core_id_t requester, cxl_id_t cxl_id, Byte* buf, SubsecondTime now, ShmemPerf *perf);
     SubsecondTime putMAC(IntPtr mac_addr, core_id_t requester, cxl_id_t cxl_id, Byte* buf, SubsecondTime now);

    public:
     CXLVNServerCntlr(MemoryManagerBase* memory_manager, ShmemPerfModel* shmem_perf_model, UInt32 cache_block_size, CXLAddressTranslator* cxl_address_tranlator, CXLCntlr* cxl_cntlr, core_id_t core_id);
     ~CXLVNServerCntlr();

     boost::tuple<SubsecondTime, HitWhere::where_t> getDataFromCXL(
         IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now,
         ShmemPerf* perf, cxl_id_t cxl_id = INVALID_CXL_ID);
     boost::tuple<SubsecondTime, HitWhere::where_t> putDataToCXL(
         IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now,
         cxl_id_t cxl_id = INVALID_CXL_ID);

     SubsecondTime getVNFromCXL(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now, ShmemPerf *perf);
     SubsecondTime updateVNToCXL(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now);

     void enablePerfModel();
     void disablePerfModel();

     friend class MEEBase;
     friend class MEENaive;
};

#endif // __CXL_VN_CNTLR_H