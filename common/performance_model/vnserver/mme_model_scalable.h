#ifndef __MME_MODEL_SCALABLE_H__
#define __MME_MODEL_SCALABLE_H__
#include "mme_model.h"
#include "queue_model.h"
#include "fixed_types.h"
#include "subsecond_time.h"
#include "dram_cntlr_interface.h"

class mmeModelScalable : public mmeModel
{
    private:
     QueueModel* m_queue_model;
     SubsecondTime m_aes_latency;
     SubsecondTime m_mult_latency;
     ComponentBandwidth m_mme_crptol_bandwidth;

     SubsecondTime m_total_queueing_delay;
     SubsecondTime m_total_access_latency;

     public:
      mmeModelScalable(core_id_t core_id, UInt32 cache_block_size);
      ~mmeModelScalable();

      SubsecondTime getAccessLatency(SubsecondTime pkt_time, UInt64 pkt_size,
                                     core_id_t requester, IntPtr address,
                                     DramCntlrInterface::access_t access_type,
                                     ShmemPerf* perf);
}
#endif  // __MME_MODEL_SCALABLE_H__