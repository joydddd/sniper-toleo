#include "mee_naive.h"
#include "config.h"
#include "config.hpp"
#include "simulator.h"

MEENaive::MEENaive(core_id_t core_id):
    MEEBase(core_id)
{
    m_mme_perf_model = new MEEPerfModel(core_id);
}

MEENaive::~MEENaive()
{
    delete m_mme_perf_model;
}

SubsecondTime MEENaive::genMAC(IntPtr address, core_id_t requester, SubsecondTime now, ShmemPerf *perf){
    SubsecondTime access_latency = m_mme_perf_model->getcryptoLatency(now, requester, MEEBase::GEN_MAC, perf);
    m_mac_gen++;
    return access_latency;
}

SubsecondTime MEENaive::encryptData(IntPtr address, core_id_t requester, SubsecondTime now){
    SubsecondTime access_latency = m_mme_perf_model->getcryptoLatency(now, requester, MEEBase::ENCRYPT, &m_dummy_shmem_perf);
    m_num_encry++;
    return access_latency;
}

SubsecondTime MEENaive::decryptData(IntPtr address, core_id_t requester, SubsecondTime now, ShmemPerf *perf){
    SubsecondTime access_latency = m_mme_perf_model->getcryptoLatency(now, requester, MEEBase::DECRYPT, perf);
    m_num_decry++;
    return access_latency;
}


void MEENaive::enablePerfModel()
{
    m_mme_perf_model->enable();
}

void MEENaive::disablePerfModel()
{
    m_mme_perf_model->disable();
} 