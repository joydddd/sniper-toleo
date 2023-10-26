#include "simulator.h"
#include "vv_perf_model.h"
#include "vv_perf_model_fixed.h"
#include "config.hpp"


VVPerfModel* VVPerfModel::createVVPerfModel(cxl_id_t cxl_id, UInt32 vn_size)
{
    String type = Sim()->getCfg()->getString("perf_model/cxl/vnserver/type");
    if (type == "fixed")
    {
        return new VVPerfModelFixed(cxl_id, vn_size);
    } else {
        LOG_PRINT_ERROR("Invalid VersionVault model type %s", type.c_str());
    }
}