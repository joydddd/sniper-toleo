#ifndef __DRAM_PERF_MODEL_MME_H__
#define __DRAM_PERF_MODEL_MME_H__

#include "dram_cntlr_interface.h"
#include "dram_perf_model.h"
#include "fixed_types.h"
#include "queue_model.h"
#include "subsecond_time.h"
#include "vn_server_perf_model.h"

class DramPerfModelMME : public DramPerfModel {
   private:
    DramPerfModel* mme_dram_model;
    DramPerfModel* vn_machine_model;
    vnServerPerfModel* vn_server;

    SubsecondTime mme_mult_delay;
    SubsecondTime mme_aes_delay;

   public:
    DramPerfModelMME(core_id_t core_id,
                     UInt32 cache_block_size, DramPerfModel* dram_model, DramPerfModel *vn_model);

    ~DramPerfModelMME();

    SubsecondTime getAccessLatency(SubsecondTime pkt_time, UInt64 pkt_size,
                                   core_id_t requester, IntPtr address,
                                   DramCntlrInterface::access_t access_type,
                                   ShmemPerf* perf);
};

#endif /* __DRAM_PERF_MODEL_MME_H__ */
