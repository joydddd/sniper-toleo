#include "simulator.h"
#include "cxl_perf_model.h"
#include "cxl_perf_model_const.h"
#include "cxl_vn_perf_model.h"
#include "cxl_perf_model_dramsim.h"
#include "cxl_vn_perf_model_dramsim.h"

CXLPerfModel* CXLPerfModel::createCXLPerfModel(cxl_id_t cxl_id, UInt64 transaction_size, bool is_vn){
    if (is_vn){
        String type = Sim()->getCfg()->getString("perf_model/cxl/vnserver/type");
        if (type == "constant")
        {
            return new CXLVNPerfModel(cxl_id, transaction_size);
        }
        else if (type == "dramsim")
        {
            // return new CXLVNPerfModelDramSim(cxl_id, transaction_size);
            // TODO: implement
            return NULL;
        } else {
            LOG_PRINT_ERROR("Invalid CXL VN model type %s", type.c_str());
        }
    } else {
        String type = Sim()->getCfg()->getString("perf_model/cxl/memory_expander_" + itostr((unsigned int)cxl_id) + "/type");
        if (type == "constant")
        {
            return new CXLPerfModelConstant(cxl_id, transaction_size);
        }
        else if (type == "dramsim")
        {
            return new CXLPerfModelDramSim(cxl_id, transaction_size);
        }
        else
        {
            LOG_PRINT_ERROR("Invalid CXL model type %s", type.c_str());
        }
    }
}