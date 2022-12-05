#include "dram_perf_model_mme.h"
#include "simulator.h"
#include "config.h"
#include "config.hpp"
#include "subsecond_time.h"

DramPerfModelMME::DramPerfModelMME(core_id_t core_id, UInt32 cache_block_size,
                                   DramPerfModel* dram_model,
                                   DramPerfModel* vn_model)
    : DramPerfModel(core_id, cache_block_size),
      mme_dram_model(dram_model),
      vn_machine_model(vn_model),
      mme_mult_delay(
          SubsecondTime::FS() *
          static_cast<uint64_t>(TimeConverter<float>::NStoFS(
              Sim()->getCfg()->getFloat("perf_model/mme/mult_latency")))),
      mme_aes_delay(
          SubsecondTime::FS() *
          static_cast<uint64_t>(TimeConverter<float>::NStoFS(
              Sim()->getCfg()->getFloat("perf_model/mme/aes_latency"))))

{
    vn_server = new vnServerPerfModel();
}

DramPerfModelMME::~DramPerfModelMME(){
    delete mme_dram_model;
    delete vn_server;
    delete vn_machine_model;
}

SubsecondTime DramPerfModelMME::getAccessLatency(
    SubsecondTime pkt_time, UInt64 pkt_size, core_id_t requester,
    IntPtr address, DramCntlrInterface::access_t access_type, ShmemPerf* perf) {

    

    SubsecondTime vn_delay;
    switch (access_type) {
        case DramCntlrInterface::READ:
            vn_delay =
                vn_server->getLatency(vnServerPerfModel::GET_VERSION_NUMBER);
            break;
        case DramCntlrInterface::WRITE:
            vn_delay =
                vn_server->getLatency(vnServerPerfModel::UPDATE_VERSION_NUMBER);
            break;
        default:
            break;
    }

    /* Read Version Number */
    vn_delay += vn_machine_model->getAccessLatency(
        pkt_time, pkt_size, requester, address, DramCntlrInterface::READ, NULL);

    /* Access Crpto data*/
    SubsecondTime cipher_delay;
    cipher_delay = mme_dram_model->getAccessLatency(
        pkt_time, pkt_size, requester, address, access_type, perf);

    /* Access tag */
    SubsecondTime tag_delay;
    tag_delay = mme_dram_model->getAccessLatency(pkt_time, pkt_size, requester,
                                                  address, access_type, perf);

    SubsecondTime total_access_time;
    switch(access_type) {
        case DramCntlrInterface::READ:
        {
            /* Maxium of (cipher_delay + decrypt), max(cipher_delay, vn_delay) + mac, tag_delay */
            SubsecondTime decryption_time = cipher_delay + mme_aes_delay;
            SubsecondTime tag_verify_time =
                (cipher_delay > vn_delay ? cipher_delay : vn_delay) +
                mme_mult_delay * 3;
            tag_verify_time =
                tag_verify_time > tag_delay ? tag_verify_time : tag_delay;
            total_access_time = tag_verify_time > decryption_time ? tag_verify_time : decryption_time;
        } break;
        case DramCntlrInterface::WRITE:
        {
            SubsecondTime max_cal_time = vn_delay > mme_aes_delay ? vn_delay : mme_aes_delay + 3 * mme_mult_delay;
            total_access_time = max_cal_time + tag_delay > cipher_delay
                                    ? tag_delay
                                    : cipher_delay;
        } break;
        default:
            break;
    }

    // Update Memory Counters
    m_num_accesses++;

    return total_access_time;
}