#ifndef __MEE_PERF_MODEL_H__
#define __MEE_PERF_MODEL_H__

#include "queue_model.h"
#include "fixed_types.h"
#include "subsecond_time.h"
#include "mee_base.h"

class MEEPerfModel {
    private:
     bool m_enabled;
     UInt64 m_num_accesses;
     core_id_t m_mee_id;

     QueueModel* m_aes_queue_model;
     UInt64 m_mee_freq; // in Hz
     ComponentPeriod m_mee_period;
     SubsecondTime m_aes_latency;
     ComponentBandwidth m_aes_bandwidth;

     SubsecondTime m_total_queueing_delay;
     SubsecondTime m_total_aes_latency;

    public:
     MEEPerfModel(core_id_t mee_id);
     ~MEEPerfModel();
     SubsecondTime getAESLatency(SubsecondTime now, core_id_t requester, MEEBase::MEE_op_t op_type, ShmemPerf *perf, int num_ares_op = 5);
     // each encrypt + mac / decrypt + mac operation contains 5 aes operations 
     void enable() { m_enabled = true; } 
     void disable() { m_enabled = false; }

     UInt64 getTotalAccesses() { return m_num_accesses; }
    
};

#endif /*__MEE_PERF_MODEL_H__*/