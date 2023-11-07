#ifndef __DRAM_VN_CNTLR_H
#define __DRAM_VN_CNTLR_H

#include "dram_cntlr_interface.h"
#include "subsecond_time.h"
#include "mee_base.h"
#include <map>

class DramMEECntlr : public DramCntlrInterface
{
    private: 
     DramCntlrInterface* m_dram_cntlr;
     MEEBase* m_mee;
     bool m_mac_enabled, m_vn_enabled;
     FILE* f_trace;
     UInt64 m_reads, m_writes;
     UInt64 m_mac_reads, m_mac_writes;
     SubsecondTime m_total_data_read_delay;
     // DEBUG:
     SubsecondTime m_total_mac_delay, m_total_dram_delay, m_total_decrypt_delay;

      /* called by mee cntlr for getting MAC locally */
     SubsecondTime getMAC(IntPtr mac_addr, core_id_t requester, Byte* buf, SubsecondTime now, ShmemPerf *perf);
     SubsecondTime putMAC(IntPtr mac_addr, core_id_t requester, Byte* buf, SubsecondTime now);

    public:
     DramMEECntlr(MemoryManagerBase* memory_manager,
                  ShmemPerfModel* shmem_perf_model, UInt32 cache_block_size,
                  CXLAddressTranslator* cxl_address_translator,
                  DramCntlrInterface* dram_cntlr, core_id_t core_id);
     ~DramMEECntlr();

     boost::tuple<SubsecondTime, HitWhere::where_t> getDataFromDram(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now, ShmemPerf *perf, bool is_virtual_address = true);
     boost::tuple<SubsecondTime, HitWhere::where_t> putDataToDram(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now, bool is_virtual_address = true);

     SubsecondTime handleVNUpdateFromCXL(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now);
     SubsecondTime handleVNverifyFromCXL(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now, ShmemPerf *perf);

     void enablePerfModel();
     void disablePerfModel();

     friend class MEEBase;
     friend class MEENaive;
};

#endif // __DRAM_VN_CNTLR_H