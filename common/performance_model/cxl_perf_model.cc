#include "simulator.h"
#include "cxl_perf_model.h"
#include "cxl_perf_model_mem.h"
#include "cxl_vn_perf_model.h"
#include "vv_perf_model.h"
#include "dram_perf_model.h"

CXLPerfModel* CXLPerfModel::createCXLPerfModel(cxl_id_t cxl_id, UInt64 transaction_size, bool is_vn){
    if (is_vn){

        return new CXLVNPerfModel(cxl_id, transaction_size);
    } else {
        return new CXLPerfModelMemoryExpander(cxl_id, transaction_size);
    }
}