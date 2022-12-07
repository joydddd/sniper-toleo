#include "simulator.h"
#include "dram_perf_model.h"
#include "dram_perf_model_constant.h"
#include "dram_perf_model_readwrite.h"
#include "dram_perf_model_normal.h"
#include "dram_perf_model_mme.h"
#include "config.hpp"

DramPerfModel* DramPerfModel::createDramPerfModel(core_id_t core_id, UInt32 cache_block_size)
{
   String type = Sim()->getCfg()->getString("perf_model/dram/type");
   bool mme_enable = Sim()->getCfg()->getBool("perf_model/dram/mme_enable");

   DramPerfModel* dram_model = NULL;
   if (type == "constant") {
       dram_model =  new DramPerfModelConstant(core_id, cache_block_size);
   } else if (type == "readwrite") {
       dram_model =  new DramPerfModelReadWrite(core_id, cache_block_size);
   } else if (type == "normal") {
       dram_model =  new DramPerfModelNormal(core_id, cache_block_size);
   } else {
       LOG_PRINT_ERROR("Invalid DRAM model type %s", type.c_str());
   }

   if (mme_enable){
       return new DramPerfModelMME(core_id, cache_block_size, dram_model);
   } else {
       return dram_model;
   }
}
