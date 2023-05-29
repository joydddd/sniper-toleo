#include "dram_cntlr_interface.h"
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

void DramCntlrInterface::handleMsgFromTagDirectory(core_id_t sender, PrL1PrL2DramDirectoryMSI::ShmemMsg* shmem_msg)
{
   PrL1PrL2DramDirectoryMSI::ShmemMsg::msg_t shmem_msg_type = shmem_msg->getMsgType();
   SubsecondTime msg_time = getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_SIM_THREAD);
   shmem_msg->getPerf()->updateTime(msg_time);

   switch (shmem_msg_type)
   {
      case PrL1PrL2DramDirectoryMSI::ShmemMsg::DRAM_READ_REQ:
      {
         IntPtr address = shmem_msg->getAddress();
         Byte data_buf[getCacheBlockSize()];
         SubsecondTime dram_latency;
         HitWhere::where_t hit_where;

         MYLOG("[dram cntlr %d] handle R REQ from [tag dir %d] @%016lx\n", getMemoryManager()->getCore()->getId(), sender, address);
         boost::tie(dram_latency, hit_where) = getDataFromDram(address, shmem_msg->getRequester(), data_buf, msg_time, shmem_msg->getPerf());

         getShmemPerfModel()->incrElapsedTime(dram_latency, ShmemPerfModel::_SIM_THREAD);
         
         if (hit_where != HitWhere::CXL){
            shmem_msg->getPerf()->updateTime(getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_SIM_THREAD),
               hit_where == HitWhere::DRAM_CACHE ? ShmemPerf::DRAM_CACHE : ShmemPerf::DRAM);
            MYLOG("[dram cntlr %d] send R REP to [tag dir %d] @%016lx\n", getMemoryManager()->getCore()->getId(), sender, address);
            getMemoryManager()->sendMsg(PrL1PrL2DramDirectoryMSI::ShmemMsg::DRAM_READ_REP,
               MemComponent::DRAM, MemComponent::TAG_DIR,
               shmem_msg->getRequester() /* requester */,
               sender /* receiver */,
               address,
               data_buf, getCacheBlockSize(),
               hit_where, shmem_msg->getPerf(), ShmemPerfModel::_SIM_THREAD);
         }
         break;
      }

      case PrL1PrL2DramDirectoryMSI::ShmemMsg::DRAM_WRITE_REQ:
      {
         MYLOG("[dram cntlr %d] handle W REQ from [tag dir %d] @%016lx\n", getMemoryManager()->getCore()->getId(), sender, shmem_msg->getAddress());
         putDataToDram(shmem_msg->getAddress(), shmem_msg->getRequester(), shmem_msg->getDataBuf(), msg_time);

         // DRAM latency is ignored on write

         break;
      }

      default:
         LOG_PRINT_ERROR("Unrecognized Shmem Msg Type: %u", shmem_msg_type);
         break;
   }
}


void 
DramCntlrInterface::handleMsgFromCXLCntlr(core_id_t tag_dir, PrL1PrL2DramDirectoryMSI::ShmemMsg* shmem_msg){
   PrL1PrL2DramDirectoryMSI::ShmemMsg::msg_t shmem_msg_type = shmem_msg->getMsgType();
   SubsecondTime msg_time = getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_SIM_THREAD);
   shmem_msg->getPerf()->updateTime(msg_time);
   switch (shmem_msg_type)
   {
      case PrL1PrL2DramDirectoryMSI::ShmemMsg::CXL_READ_REP:
      {
         IntPtr address = shmem_msg->getAddress();
         MYLOG("[dram cntlr %d] handle CXL R REP @%016lx\n", getMemoryManager()->getCore()->getId(), address);

         // write to dram cache. Latency is ignored for cache write.
         handleDataFromCXL(shmem_msg->getAddress(), shmem_msg->getRequester(), shmem_msg->getDataBuf(), msg_time);
         
         // forward message to TAG_DIR
         MYLOG("[dram cntlr %d] send R REP (forward cxl) to [tag dir %d] @%016lx\n", getMemoryManager()->getCore()->getId(), tag_dir, address);
         getMemoryManager()->sendMsg(PrL1PrL2DramDirectoryMSI::ShmemMsg::DRAM_READ_REP,
                  MemComponent::DRAM, MemComponent::TAG_DIR,
                  shmem_msg->getRequester(), /* requester */
                  tag_dir,           /* receiver */
                  shmem_msg->getAddress(), shmem_msg->getDataBuf(),
                  getCacheBlockSize(), 
                  shmem_msg->getWhere(),
                  shmem_msg->getPerf(), ShmemPerfModel::_SIM_THREAD);
         break;
      }

      default:
         LOG_PRINT_ERROR("Unrecognized Shmem Msg Type: %u", shmem_msg_type);
         break;
   }
}