#ifndef __CXL_CNTLR_INTERFACE_H
#define __CXL_CNTLR_INTERFACE_H

#include "fixed_types.h"
#include "subsecond_time.h"
#include "hit_where.h"
#include "shmem_msg.h"
#include "cxl_address_translator.h"
#include "shmem_perf.h"

#include "boost/tuple/tuple.hpp"
#include "simulator.h"
#include "config.h"
#include "config.hpp"

class MemoryManagerBase;
class ShmemPerfModel;
class ShmemPerf;

class CXLCntlrInterface
{
   protected:
      MemoryManagerBase* m_memory_manager;
      ShmemPerfModel* m_shmem_perf_model;
      UInt32 m_cache_block_size, m_vn_block_size; // in bytes
      bool m_vn_server_enabled;
      CXLAddressTranslator* m_address_translator;

      ShmemPerf m_dummy_shmem_perf;

      UInt32 getCacheBlockSize() { return m_cache_block_size; }
      UInt32 getVNBlockSize() { return m_vn_block_size; }
      MemoryManagerBase* getMemoryManager() { return m_memory_manager; }
      ShmemPerfModel* getShmemPerfModel() { return m_shmem_perf_model; }

   public:
      typedef enum
      {
         READ = 0,
         WRITE,
         VN_READ,
         VN_UPDATE,
         NUM_ACCESS_TYPES
      } access_t;

      CXLCntlrInterface(MemoryManagerBase* memory_manager, ShmemPerfModel* shmem_perf_model, UInt32 cache_block_size, CXLAddressTranslator* cxl_address_tranlator)
         : m_memory_manager(memory_manager)
         , m_shmem_perf_model(shmem_perf_model)
         , m_cache_block_size(cache_block_size)
         , m_vn_block_size(0)
         , m_vn_server_enabled(Sim()->getCfg()->getBool("perf_model/cxl/vnserver/enable"))
         , m_address_translator(cxl_address_tranlator)
      {
         if (m_vn_server_enabled)
            m_vn_block_size = (Sim()->getCfg()->getInt("perf_model/mee/vn_length") -1 )/ 8 + 1;
      }
      virtual ~CXLCntlrInterface() {}

      virtual boost::tuple<SubsecondTime, HitWhere::where_t> getDataFromCXL(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now, ShmemPerf *perf) = 0;
      virtual boost::tuple<SubsecondTime, HitWhere::where_t> putDataToCXL(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now) = 0;
      virtual SubsecondTime getVNFromCXL(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now, ShmemPerf *perf) = 0;
      virtual SubsecondTime updateVNToCXL(IntPtr address, core_id_t requester, Byte* data_buf, SubsecondTime now) = 0;

      virtual void enablePerfModel() = 0;
      virtual void disablePerfModel() = 0;
      
      void handleMsgFromDram(core_id_t sender, PrL1PrL2DramDirectoryMSI::ShmemMsg* shmem_msg);
};

#endif // __CXL_CNTLR_INTERFACE_H
