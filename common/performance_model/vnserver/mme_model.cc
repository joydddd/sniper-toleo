// #include "simulator.h"
// #include "mme_model.h"
// #include "config.hpp"

// mmeModel* mmeModel::createMMEModel(core_id_t core_id, UInt32 cache_block_size){
//     String type = Sim()->getCfg()->getString("perf_model/mme/type");
//     if (type == "scalable"){ // encryption only
//         return new mmeModelScalable(core_id, cache_block_size);
//     }
//     else if (type == "vnserver") { // with version number server
//         return new mmeModelvnserver(core_id, cache_block_size);
//     }
//     else {
//         LOG_PRINT_ERROR("Invalid MME model type %s", type.c_str());
//     }
// }
