#include "simulator.h"
#include "dram_perf_model.h"
#include "dram_perf_model_constant.h"
#include "dram_perf_model_readwrite.h"
#include "dram_perf_model_normal.h"
#include "dram_perf_model_dramsim.h"
#include "dram_perf_model_ddr.h"
#include "config.hpp"

DramPerfModel* DramPerfModel::createDramPerfModel(core_id_t core_id, UInt32 cache_block_size, DramType dram_type)
{
   String config_name;
   String type;
   if (dram_type == SYSTEM_DRAM){
      config_name = "perf_model/dram/type";
      type = Sim()->getCfg()->getString(config_name);
   } else if (dram_type == CXL_MEMORY){
      config_name = "perf_model/cxl/memory_expander_" + itostr((unsigned int)core_id) + "/dram/type";
      type = Sim()->getCfg()->getString(config_name);
   } else if (dram_type == CXL_VN){
      config_name = "perf_model/cxl/vnserver/dram/type";
      type = Sim()->getCfg()->getString(config_name);
   } else {
       if (dram_type == DDR_ONLY) type = "ddr-only";
       else LOG_PRINT_ERROR("Invalid DRAM perf model type %d", dram_type);
   }

   if (type == "constant")
   {
      return new DramPerfModelConstant(core_id, cache_block_size, dram_type);
   }
   else if (type == "readwrite")
   {
      LOG_ASSERT_ERROR(dram_type == SYSTEM_DRAM, "Readwrite DRAM model only supported for system DRAM");
      return new DramPerfModelReadWrite(core_id, cache_block_size);
   }
   else if (type == "normal")
   {
      LOG_ASSERT_ERROR(dram_type == SYSTEM_DRAM, "Readwrite DRAM model only supported for system DRAM");
      return new DramPerfModelNormal(core_id, cache_block_size);
   } else if (type == "dramsim")
   {
      return new DramPerfModelDramSim(core_id, cache_block_size, dram_type);
   } else if (type == "ddr-only")
   {
      return new DramPerfModelDDR(core_id, cache_block_size);
   }
   else
   {
      LOG_PRINT_ERROR("Invalid DRAM model type %s", type.c_str());
   }
}
