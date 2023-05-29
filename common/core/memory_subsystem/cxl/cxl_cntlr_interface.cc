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
   fprintf(stderr, "[%s] %d%cdr %-25s@%3u: ",                                                      \
   itostr(getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_SIM_THREAD)).c_str(),      \
         getMemoryManager()->getCore()->getId(),                                          \
         Sim()->getCoreManager()->amiUserThread() ? '^' : '_', __PRETTY_FUNCTION__, __LINE__);   \
   fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); fflush(stderr); }
#else
#  define MYLOG(...) {}
#endif

void CXLCntlrInterface::handleMsgFromDram(core_id_t sender, PrL1PrL2DramDirectoryMSI::ShmemMsg* shmem_msg)
{
   PrL1PrL2DramDirectoryMSI::ShmemMsg::msg_t shmem_msg_type = shmem_msg->getMsgType();
   SubsecondTime msg_time = getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_SIM_THREAD);
   shmem_msg->getPerf()->updateTime(msg_time);

   switch (shmem_msg_type)
   {
      case PrL1PrL2DramDirectoryMSI::ShmemMsg::CXL_READ_REQ:
      {
         IntPtr address = shmem_msg->getAddress();
         Byte data_buf[getCacheBlockSize()];
         SubsecondTime cxl_latency;
         HitWhere::where_t hit_where;

         IntPtr linear_address = m_address_translator->getLinearAddress(address);
         cxl_id_t cxl_id = m_address_translator->getHome(address);
         
         MYLOG("[cxl %d] handle R @%016lx, ##local addr## %016lx\n", cxl_id, address, linear_address);

         boost::tie(cxl_latency, hit_where) = getDataFromCXL(linear_address, cxl_id, shmem_msg->getRequester(), data_buf, msg_time, shmem_msg->getPerf());

         getShmemPerfModel()->incrElapsedTime(cxl_latency, ShmemPerfModel::_SIM_THREAD);

         shmem_msg->getPerf()->updateTime(getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_SIM_THREAD),
            ShmemPerf::CXL);

         MYLOG("[cxl %d] send R REP to [dram cntlr %d] @%016lx\n", cxl_id, sender, address);
         getMemoryManager()->sendMsg(PrL1PrL2DramDirectoryMSI::ShmemMsg::CXL_READ_REP,
               MemComponent::CXL, MemComponent::DRAM,
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
         cxl_id_t cxl_id = m_address_translator->getHome(address);
         IntPtr linear_address = m_address_translator->getLinearAddress(address);
         MYLOG("[cxl %d] handle W @%016lx, ##local addr## %016lx\n", cxl_id, address, linear_address);
         putDataToCXL(linear_address, cxl_id, shmem_msg->getRequester(), shmem_msg->getDataBuf(), msg_time);

         // DRAM latency is ignored on write

         break;
      }

      default:
         LOG_PRINT_ERROR("Unrecognized Shmem Msg Type: %u", shmem_msg_type);
         break;
   }
}
