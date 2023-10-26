#include "cxl_cntlr_interface.h"
#include "memory_manager.h"
#include "shmem_msg.h"
#include "shmem_perf.h"
#include "log.h"

#if 0
#  define MYLOG_ENABLED
   extern Lock iolock;
#  include "core_manager.h"
#  include "simulator.h"
#  define MYLOG(...) {                                                                    \
   ScopedLock l(iolock);                                                                  \
   fflush(stderr);                                                                        \
   fprintf(stderr, "[cxl cntlr %d]", getMemoryManager()->getCore()->getId());            \
   fprintf(stderr, __VA_ARGS__);                                                          \
   fprintf(stderr, " [msg: %s] [perf latest: %s]\n",                                     \
   itostr(getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_SIM_THREAD)).c_str(),      \
   itostr(shmem_msg->getPerf()->getLastTime()).c_str());                                          \
   fprintf(stderr, "\n"); fflush(stderr); }
#else
#  define MYLOG(...) {}
#endif

void CXLCntlrInterface::handleMsgFromTagDirectory(core_id_t sender, PrL1PrL2DramDirectoryMSI::ShmemMsg* shmem_msg)
{
   PrL1PrL2DramDirectoryMSI::ShmemMsg::msg_t shmem_msg_type = shmem_msg->getMsgType();
   SubsecondTime msg_time = getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_SIM_THREAD);
   shmem_msg->getPerf()->updateTime(msg_time);

   IntPtr address = shmem_msg->getAddress();
   // DEBUG:Just in case, check the address is located in CXL
   if (m_address_translator){
      cxl_id_t cxl_id = m_address_translator->getHome(address);
      LOG_ASSERT_ERROR(cxl_id != HOST_CXL_ID, "Home for address %p is not CXL: addr_home %d", address, cxl_id);
   }

   switch (shmem_msg_type)
   {
      case PrL1PrL2DramDirectoryMSI::ShmemMsg::CXL_READ_REQ:
      {
         
         Byte data_buf[getCacheBlockSize()];
         SubsecondTime cxl_latency;
         HitWhere::where_t hit_where;

         
         MYLOG("[cxl] 0x%012lX handle R",address);

         boost::tie(cxl_latency, hit_where) = getDataFromCXL(address, shmem_msg->getRequester(), data_buf, msg_time, shmem_msg->getPerf());
         LOG_ASSERT_ERROR(hit_where == HitWhere::CXL || hit_where == HitWhere::CXL_VN, "hit_where(%u)", hit_where);

         getShmemPerfModel()->incrElapsedTime(cxl_latency, ShmemPerfModel::_SIM_THREAD);

         shmem_msg->getPerf()->updateTime(getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_SIM_THREAD),
            ShmemPerf::CXL);

         MYLOG("[cxl] 0x%012lX send R REP to Tag Directory", address);
         getMemoryManager()->sendMsg(PrL1PrL2DramDirectoryMSI::ShmemMsg::CXL_READ_REP,
               MemComponent::CXL, MemComponent::TAG_DIR,
               shmem_msg->getRequester() /* requester */,
               sender                     /* receiver */,
               address,
               data_buf, getCacheBlockSize(),
               hit_where, shmem_msg->getPerf(), ShmemPerfModel::_SIM_THREAD);
         break;
      }

      case PrL1PrL2DramDirectoryMSI::ShmemMsg::CXL_WRITE_REQ:
      {
         IntPtr address = shmem_msg->getAddress();
         
         MYLOG("[cxl] 0x%012lX handle W ", address);
         putDataToCXL(address, shmem_msg->getRequester(), shmem_msg->getDataBuf(), msg_time);

         // DRAM latency is ignored on write

         break;
      }

      default:
         LOG_PRINT_ERROR("Unrecognized Shmem Msg Type: %u", shmem_msg_type);
         break;
   }
}


void CXLCntlrInterface::handleMsgFromDram(core_id_t sender, PrL1PrL2DramDirectoryMSI::ShmemMsg* shmem_msg)
{
   PrL1PrL2DramDirectoryMSI::ShmemMsg::msg_t shmem_msg_type = shmem_msg->getMsgType();
   SubsecondTime msg_time = getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_SIM_THREAD);
   shmem_msg->getPerf()->updateTime(msg_time);

   switch (shmem_msg_type)
   {
      case PrL1PrL2DramDirectoryMSI::ShmemMsg::CXL_VN_REQ:
      {
         LOG_ASSERT_ERROR(m_vn_server_enabled, "CXL VN Server is not enabled, cannot handle CXL_VN_REQ");
         IntPtr address = shmem_msg->getAddress();
         Byte data_buf[getVNBlockSize()];

         SubsecondTime dram_time;
         memcpy(&dram_time, shmem_msg->getDataBuf(), sizeof(SubsecondTime));

         MYLOG("[cxl] 0x%012lX handle VN REQ. dram time %lu", address, dram_time.getNS());
         SubsecondTime vn_access_latency = getVNFromCXL(address, shmem_msg->getRequester(), data_buf, msg_time, shmem_msg->getPerf());
         getShmemPerfModel()->incrElapsedTime(vn_access_latency, ShmemPerfModel::_SIM_THREAD);
         shmem_msg->getPerf()->updateTime(getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_SIM_THREAD), ShmemPerf::VN);

         MYLOG("[cxl] 0x%012lX send VN REP to [dram cntlr %d] ", address, sender);
         getMemoryManager()->sendMsg(PrL1PrL2DramDirectoryMSI::ShmemMsg::CXL_VN_REP,
               MemComponent::CXL, MemComponent::DRAM,
               shmem_msg->getRequester() /* requester */,
               sender                     /* receiver */,
               address,
               (Byte*)&dram_time, sizeof(SubsecondTime),
               shmem_msg->getWhere(), shmem_msg->getPerf(), ShmemPerfModel::_SIM_THREAD);
         break;
      }

      case PrL1PrL2DramDirectoryMSI::ShmemMsg::CXL_VN_UPDATE:
      {
         LOG_ASSERT_ERROR(m_vn_server_enabled, "CXL VN Server is not enabled, cannot handle CXL_VN_UPDATE");
         IntPtr address = shmem_msg->getAddress();
         Byte data_buf[getVNBlockSize()];
         MYLOG("[cxl] 0x%012lX handle VN UPDATE", address);
         SubsecondTime vn_access_latency = updateVNToCXL(address, shmem_msg->getRequester(), data_buf, msg_time);
         getShmemPerfModel()->incrElapsedTime(vn_access_latency, ShmemPerfModel::_SIM_THREAD);
         // there is no perf model for write requests. 

         MYLOG("[cxl] 0x%012lX send VN REP to [dram cntlr %d]", address, sender);
         getMemoryManager()->sendMsg(PrL1PrL2DramDirectoryMSI::ShmemMsg::CXL_VN_UPDATE_REP,
               MemComponent::CXL, MemComponent::DRAM,
               shmem_msg->getRequester() /* requester */,
               sender                     /* receiver */,
               address,
               data_buf, getVNBlockSize(),
               shmem_msg->getWhere(), &m_dummy_shmem_perf, ShmemPerfModel::_SIM_THREAD);
         break;
      }

      default:
         LOG_PRINT_ERROR("Unrecognized Shmem Msg Type: %u", shmem_msg_type);
         break;
   }
}
