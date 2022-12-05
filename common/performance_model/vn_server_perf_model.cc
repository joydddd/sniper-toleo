#include "vn_server_perf_model.h"
#include "config.h"
#include "config.hpp"
#include "simulator.h"
#include "stats.h"
#include "log.h"

vnServerPerfModel::vnServerPerfModel()
: vn_mult_delay(
        SubsecondTime::FS() *
        static_cast<uint64_t>(TimeConverter<float>::NStoFS(
            Sim()->getCfg()->getFloat("perf_model/vn_server/mult_latency"))))
, vn_access_cost(
        SubsecondTime::FS() *
        static_cast<uint64_t>(TimeConverter<float>::NStoFS(
            Sim()->getCfg()->getFloat("perf_model/vn_server/access_latency"))))
{

}

SubsecondTime vnServerPerfModel::getLatency(vnActions_t action) {
    switch (action)
    {
    case GET_VERSION_NUMBER:
        return vn_mult_delay * 3;
    case UPDATE_VERSION_NUMBER:
        return vn_mult_delay * 3;
    default:
        break;
    }
}