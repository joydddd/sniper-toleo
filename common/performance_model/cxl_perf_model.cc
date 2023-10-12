#include "simulator.h"
#include "cxl_perf_model.h"
#include "cxl_perf_model_mem.h"
#include "cxl_vn_perf_model.h"
#include "dram_perf_model.h"

CXLPerfModel* CXLPerfModel::createCXLPerfModel(cxl_id_t cxl_id, UInt64 transaction_size, bool is_vn){
    if (is_vn){
        // String config_prefix = "perf_model/cxl/vnserver";

        // return new CXLVNPerfModel(cxl_id, transaction_size);

        // = Sim()->getCfg()->getString("perf_model/cxl/vnserver/type");
        // if (type == "constant")
        // {
        //     return new CXLVNPerfModel(cxl_id, transaction_size);
        // }
        // else if (type == "dramsim")
        // {
        //     // return new CXLVNPerfModelDramSim(cxl_id, transaction_size);
        //     // TODO: implement
        //     return NULL;
        // } else {
        //     LOG_PRINT_ERROR("Invalid CXL VN model type %s", type.c_str());
        // }
    } else {
        DramPerfModel* dram_perf_model = DramPerfModel::createDramPerfModel(
            cxl_id, transaction_size/8, DramType::CXL_MEMORY);
        return new CXLPerfModelMemoryExpander(cxl_id, transaction_size, dram_perf_model);
    }
}