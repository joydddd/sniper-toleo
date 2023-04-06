// #ifndef __DRAM_PERF_MODEL_MME_H__
// #define __DRAM_PERF_MODEL_MME_H__

// #include "dram_cntlr_interface.h"
// #include "dram_perf_model.h"
// #include "fixed_types.h"
// #include "queue_model.h"
// #include "subsecond_time.h"
// #include "vn_server_perf_model.h"

// class DramPerfModelMME : public DramPerfModel {
//    private:
//     DramPerfModel* mme_dram_model;
//     QueueModel* mme_queue_model;
//     vnServerPerfModel* vn_server;

//     ComponentBandwidth mme_vn_bandwidth;
//     SubsecondTime mme_mult_delay;
//     SubsecondTime mme_aes_delay;

//     SubsecondTime mme_total_vn_delay;
//     SubsecondTime mme_total_read_latency;
//     SubsecondTime mme_total_write_latency;

//     UInt64 mme_dram_reads, mme_dram_writes; 

//     SubsecondTime getvnLatency(
//         SubsecondTime pkt_time, core_id_t requester, IntPtr address,
//         vnServerPerfModel::vnActions_t access_type, ShmemPerf* perf);

//    public:
//     DramPerfModelMME(core_id_t core_id,
//                      UInt32 cache_block_size);

//     ~DramPerfModelMME();

//     SubsecondTime getAccessLatency(SubsecondTime pkt_time, UInt64 pkt_size,
//                                    core_id_t requester, IntPtr address,
//                                    DramCntlrInterface::access_t access_type,
//                                    ShmemPerf* perf);
// };

// #endif /* __DRAM_PERF_MODEL_MME_H__ */
