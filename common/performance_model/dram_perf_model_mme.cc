#include "dram_perf_model_mme.h"
#include "config.h"
#include "config.hpp"
#include "dram_perf_model_constant.h"
#include "dram_perf_model_normal.h"
#include "dram_perf_model_readwrite.h"
#include "shmem_perf.h"
#include "simulator.h"
#include "stats.h"
#include "subsecond_time.h"

DramPerfModelMME::DramPerfModelMME(core_id_t core_id, UInt32 cache_block_size)
    : DramPerfModel(core_id, cache_block_size),
      mme_dram_model(NULL),
      mme_queue_model(NULL),
      vn_server(NULL),
      mme_vn_bandwidth(
          Sim()->getCfg()->getFloat("perf_model/mme/per_mme_bandwidth") *
          vnServerPerfModel::vn_request_width),  // In terms of vn channel
                                                 // bandwidth.
      mme_mult_delay(
          SubsecondTime::FS() *
          static_cast<uint64_t>(TimeConverter<float>::NStoFS(
              Sim()->getCfg()->getFloat("perf_model/mme/mult_latency")))),
      mme_aes_delay(
          SubsecondTime::FS() *
          static_cast<uint64_t>(TimeConverter<float>::NStoFS(
              Sim()->getCfg()->getFloat("perf_model/mme/aes_latency")))),
      mme_total_vn_delay(SubsecondTime::Zero()),
      mme_total_access_latency(SubsecondTime::Zero())

{
    mme_dram_model =
        DramPerfModel::createDramPerfModel(core_id, cache_block_size, false);
    mme_dram_model->enable();
    vn_server = new vnServerPerfModel();
    if (Sim()->getCfg()->getBool("perf_model/mme/queue_model/enabled")){
        mme_queue_model = QueueModel::create(
            "vn-queue", core_id,
            Sim()->getCfg()->getString("perf_model/mme/queue_model/type"),
            mme_vn_bandwidth.getRoundedLatency(
                vnServerPerfModel::vn_request_width));
    }

    registerStatsMetric("mme", core_id, "total-access-latency",
                        &mme_total_access_latency);
    registerStatsMetric("mme", core_id, "total-vn-delay",
                        &mme_total_vn_delay);
    registerStatsMetric("dram", core_id, "reads", &mme_dram_reads);
    registerStatsMetric("dram", core_id, "writes", &mme_dram_writes);
}

DramPerfModelMME::~DramPerfModelMME(){
    delete mme_dram_model;
    delete vn_server;
    if (mme_queue_model)
    {
        delete mme_queue_model;
        mme_queue_model = NULL;
    }
}

SubsecondTime DramPerfModelMME::getvnLatency(
    SubsecondTime pkt_time, core_id_t requester, IntPtr address,
    vnServerPerfModel::vnActions_t access_type, ShmemPerf *perf) {
    SubsecondTime processing_time =
        mme_vn_bandwidth.getRoundedLatency(vnServerPerfModel::vn_request_width);

    // Compute Queue Delay
    SubsecondTime queue_delay;
    if (mme_queue_model)
    {
        queue_delay = mme_queue_model->computeQueueDelay(
            pkt_time, processing_time, requester);
    }
    else 
    {
        queue_delay = SubsecondTime::Zero();
    }

    SubsecondTime vn_delay = vn_server->getLatency(access_type);

    SubsecondTime checksum_delay = mme_mult_delay * 3; // nouce is calculated previously. 

    SubsecondTime access_latency =
        queue_delay + processing_time + vn_delay + checksum_delay;
    
    if (perf) {
        perf->updateTime(pkt_time + queue_delay, ShmemPerf::VN_QUEUE);
        perf->updateTime(pkt_time + access_latency, ShmemPerf::VN_DEVICE);
    }
    // fprintf(stderr, "VN latency %d ns, processing_tie %d ns, vn_delay %d ns\n",
    //         access_latency.getNS(), processing_time.getNS(), vn_delay.getNS());
    return access_latency;
}

SubsecondTime DramPerfModelMME::getAccessLatency(
    SubsecondTime pkt_time, UInt64 pkt_size, core_id_t requester,
    IntPtr address, DramCntlrInterface::access_t access_type, ShmemPerf* perf) {


    SubsecondTime vn_delay, cipher_delay, tag_delay, access_time;
    switch (access_type) {
        case DramCntlrInterface::READ:{
            /* Access version number, cipher text and tag at the same time */
            vn_delay =
                getvnLatency(pkt_time, requester, address,
                             vnServerPerfModel::GET_VERSION_NUMBER, perf);
            cipher_delay = mme_dram_model->getAccessLatency(
                pkt_time, pkt_size, requester, address, access_type, perf);
            tag_delay = mme_dram_model->getAccessLatency(
                pkt_time, pkt_size, requester, address, access_type, perf);
            mme_dram_reads += 2;
            /* decrypt cipher text */
            SubsecondTime decryption_time = cipher_delay + mme_aes_delay;
            /** verify tag
             * cipher -- mult -- mult -->   --- mult ---> tag
             * vn -------- aes ----------> |
            */

            SubsecondTime tag_verify_time =
                (cipher_delay + 2 * mme_mult_delay > vn_delay + mme_aes_delay
                     ? cipher_delay + 2 * mme_mult_delay
                     : vn_delay + mme_aes_delay) +
                mme_mult_delay;
            tag_verify_time =
                tag_verify_time > tag_delay ? tag_verify_time : tag_delay;
            access_time = tag_verify_time > decryption_time ? tag_verify_time
                                                            : decryption_time;

            // fprintf(stderr,
            //         "MME read time %lu ns, tag_delay %lu ns, cipher_delay %lu "
            //         "ns, vn_delay %lu ns\n",
            //         access_time.getNS(), tag_delay.getNS(),
            //         cipher_delay.getNS(), vn_delay.getNS());
        } break;
        case DramCntlrInterface::WRITE:{
            /* Access version number from vn machine and write cipher */
            vn_delay =
                getvnLatency(pkt_time, requester, address,
                             vnServerPerfModel::UPDATE_VERSION_NUMBER, perf);
            cipher_delay =
                mme_aes_delay + mme_dram_model->getAccessLatency(
                                    pkt_time + mme_aes_delay, pkt_size,
                                    requester, address, access_type, perf);
            /** calculate tag
             * cipher -- mult -- mult -->   --- mult ---> tag
             * vn -------- aes ----------> |
             */
            SubsecondTime cal_tag =
                (mme_mult_delay * 2 > vn_delay + mme_aes_delay
                     ? mme_mult_delay * 2
                     : vn_delay + mme_aes_delay) +
                mme_mult_delay;
            
            /* Write tag to dram */
            SubsecondTime write_tag_delay = mme_dram_model->getAccessLatency(
                pkt_time + cal_tag, pkt_size, requester, address, access_type,
                perf);
            access_time = cipher_delay > write_tag_delay + cal_tag
                              ? cipher_delay
                              : write_tag_delay + cal_tag;
            mme_dram_writes += 2;
        } break;
        default:
            break;
    }

    // Update perf
    perf->updateTime(pkt_time, ShmemPerf::MME);
    perf->updateTime(pkt_time + access_time, ShmemPerf::MME_DEVICE);

    // Update Memory Counters
    m_num_accesses++;
    mme_total_vn_delay += vn_delay;
    mme_total_access_latency += access_time;

    return access_time;
}