#ifndef __DRAM_INVISIMEM_CNTLR_H__
#define __DRAM_INVISIMEM_CNTLR_H__

// Define to re-enable DramAccessCount
//#define ENABLE_DRAM_ACCESS_COUNT

#include <unordered_map>

#include "dram_perf_model.h"
#include "shmem_msg.h"
#include "shmem_perf.h"
#include "fixed_types.h"
#include "cxl_cntlr_interface.h"
#include "memory_manager_base.h"
#include "dram_cntlr_interface.h"
#include "subsecond_time.h"

#include <vector>



class DramInvisiMemCntlr : public DramCntlrInterface
{
    private:
     UInt64 m_read_req_size, m_read_res_size; // in bytes
     UInt64 m_write_req_size, m_write_res_size; // in bytes
     UInt64 m_metadata_per_cl; // in bytes

     DramPerfModel* m_ddr_perf_model;
     DramPerfModel* m_hmc_perf_model;

     typedef std::unordered_map<IntPtr, UInt64> AccessCountMap;
     UInt64 m_reads, m_writes;  // data reads and writes
     UInt64 m_hmc_reads, m_hmc_writes;
     SubsecondTime m_total_data_read_delay;
     // DEBUG:
     SubsecondTime m_total_mac_delay, m_total_dram_delay, m_total_decrypt_delay;

     FILE* f_trace;

     SubsecondTime runHMCPerfModel(core_id_t requester, SubsecondTime time,
                                    IntPtr address, /* local physical address */
                                    DramCntlrInterface::access_t access_type,
                                    ShmemPerf* perf, 
                                    bool is_virtual_addr = true);
     SubsecondTime runDDRPerfModel(core_id_t requester, SubsecondTime time,
                                    IntPtr address,
                                    DramCntlrInterface::access_t access_type,
                                    ShmemPerf* perf,
                                    bool is_virtual_addr = true);

    public:
        DramInvisiMemCntlr(MemoryManagerBase* memory_manager,
            ShmemPerfModel* shmem_perf_model,
            UInt32 cache_block_size,
            CXLAddressTranslator* cxl_address_translator);

        ~DramInvisiMemCntlr();

        void enablePerfModel();
        void disablePerfModel();

        

        // Run DRAM performance model. Pass in begin time, returns latency
        // If Data is located on DRAM, handle data request. else send request to CXL
        boost::tuple<SubsecondTime, HitWhere::where_t> getDataFromDram(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now, ShmemPerf *perf, bool is_virtual_addr = true);
        boost::tuple<SubsecondTime, HitWhere::where_t> putDataToDram(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now, bool is_virtual_addr = true);
        SubsecondTime handleVNUpdateFromCXL(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now) { LOG_ASSERT_ERROR(false, "handleVNUpdateFromCXL not implemented in DramCntlr"); }
        SubsecondTime handleVNverifyFromCXL(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now, ShmemPerf *perf) { LOG_ASSERT_ERROR(false, "handleVNUpdateFromCXL not implemented in DramCntlr"); }
};

#endif // __DRAM_INVISIMEM_CNTLR_H__
