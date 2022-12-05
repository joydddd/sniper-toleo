#ifndef __VN_SERVER_PERF_MODEL_H__
#define __VN_SERVER_PERF_MODEL_H__

#include "config.h"
#include "log.h"
#include "queue_model.h"

class vnServerPerfModel 
{
    private:
     QueueModel* vn_queue_model; // TODO: if there're more than 4 memory controllers
     SubsecondTime vn_mult_delay;
     SubsecondTime vn_access_cost;

    public : vnServerPerfModel();
     ~vnServerPerfModel() {}

     enum vnActions_t { GET_VERSION_NUMBER, UPDATE_VERSION_NUMBER };

     SubsecondTime getLatency(vnActions_t action);
};
/**
 * Assumings: 10 mults can be done each cycle (cycle = memory clock)
 * Among which: 8 are parallel, 2 are serial. => 3 mult delays. 
*/

/**
 * Speculative version number send: send vn after read, followed by tag after mult delay. 
 * MME can start version number verification before vn_mac arrive at MME
*/

#endif /* __MME_VN_PERF_MODEL_H__ */
