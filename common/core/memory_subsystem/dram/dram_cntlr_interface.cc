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

   IntPtr address = shmem_msg->getAddress();

   // DEBUG:Just in case, check the address is located in DRAM
   if (m_address_translator){
      cxl_req_t cxl_req;
      cxl_req.cxl_id = m_address_translator->getHome(address);
      LOG_ASSERT_ERROR(cxl_req.cxl_id == HOST_CXL_ID, "Home for address %p is not DRAM: addr_home %d", address, cxl_req.cxl_id);
   }

   switch (shmem_msg_type)
   {
      case PrL1PrL2DramDirectoryMSI::ShmemMsg::DRAM_READ_REQ:
      {
         Byte data_buf[getCacheBlockSize()];
         SubsecondTime dram_latency;
         HitWhere::where_t hit_where;

         MYLOG("[dram cntlr %d] handle R REQ from [tag dir %d] @%016lx\n", getMemoryManager()->getCore()->getId(), sender, address);
         boost::tie(dram_latency, hit_where) = getDataFromDram(address, shmem_msg->getRequester(), data_buf, msg_time, shmem_msg->getPerf());

         LOG_ASSERT_ERROR(hit_where == HitWhere::DRAM || hit_where == HitWhere::DRAM_CACHE || hit_where == HitWhere::CXL_VN, "hit_where(%u)", hit_where);

         
         // send reply to dram directory if no need to fetch vn from CXL. 
         if (hit_where == HitWhere::DRAM_CACHE || hit_where == HitWhere::DRAM){
            getShmemPerfModel()->incrElapsedTime(dram_latency, ShmemPerfModel::_SIM_THREAD);
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
         } else if (hit_where == HitWhere::CXL_VN){ // need to fetch VN from CXL
            getShmemPerfModel()->incrElapsedTime(dram_latency, ShmemPerfModel::_SIM_THREAD);
            shmem_msg->getPerf()->updateTime(getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_SIM_THREAD),
               ShmemPerf::VN);
            //DEBUG:
            fprintf(stderr, "Addr %016lx fetching VN from VV: msg time %lu, perf latest time %lu, new msg time %lu\n", address, msg_time.getNS(), shmem_msg->getPerf()->getLastTime().getNS(), getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_SIM_THREAD).getNS());
            getMemoryManager()->sendMsg(
            PrL1PrL2DramDirectoryMSI::ShmemMsg::CXL_VN_REQ, 
            MemComponent::DRAM, /* sender component */
            MemComponent::CXL, /* reciever component*/
            shmem_msg->getRequester(),    /* requester */
            0,                            /* receiver */
            address, NULL, 0, HitWhere::where_t::DRAM, 
            shmem_msg->getPerf(), ShmemPerfModel::_SIM_THREAD);
            return;
         }
         break;
      }

      case PrL1PrL2DramDirectoryMSI::ShmemMsg::DRAM_WRITE_REQ:
      {
         SubsecondTime dram_latency;
         HitWhere::where_t hit_where;
         
         MYLOG("[dram cntlr %d] handle W REQ from [tag dir %d] @%016lx\n", getMemoryManager()->getCore()->getId(), sender, shmem_msg->getAddress());
         boost::tie(dram_latency, hit_where) = putDataToDram(shmem_msg->getAddress(), shmem_msg->getRequester(), shmem_msg->getDataBuf(), msg_time);

         // request updated VN on any writes if VN Server is enabled. 
         if (hit_where == HitWhere::CXL_VN){
            MYLOG("[%d]updateVN @ %016lx ", requester, address);
            /* send msg to get updated vn */
            getShmemPerfModel()->incrElapsedTime(dram_latency, ShmemPerfModel::_SIM_THREAD);
            shmem_msg->getPerf()->updateTime(getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_SIM_THREAD), ShmemPerf::VN);
            // write requests don't have perf model
            getMemoryManager()->sendMsg(
                PrL1PrL2DramDirectoryMSI::ShmemMsg::CXL_VN_UPDATE,
                MemComponent::DRAM, MemComponent::CXL,
                shmem_msg->getRequester(), /* requester */
                0,                         /* receiver */
                address, NULL, 0, HitWhere::UNKNOWN, &m_dummy_shmem_perf,
                ShmemPerfModel::_SIM_THREAD);
         }

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
      case PrL1PrL2DramDirectoryMSI::ShmemMsg::CXL_VN_REP:
      {
         Byte data_buf[getCacheBlockSize()];
         IntPtr address = shmem_msg->getAddress();
         MYLOG("[dram cntlr %d] handle CXL VN REP @%016lx\n", getMemoryManager()->getCore()->getId(), address);
         
         // verify VN and write to dram_cache. 
         SubsecondTime verify_latency = handleVNverifyFromCXL(address, shmem_msg->getRequester(), data_buf, msg_time, shmem_msg->getPerf());

         // forward message to TAG_DIR
         getShmemPerfModel()->incrElapsedTime(verify_latency, ShmemPerfModel::_SIM_THREAD);
         shmem_msg->getPerf()->updateTime(getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_SIM_THREAD), ShmemPerf::MEE);
         MYLOG("[dram cntlr %d] send R REP (forward cxl) to [tag dir %d] @%016lx\n", getMemoryManager()->getCore()->getId(), tag_dir, address);
         getMemoryManager()->sendMsg(PrL1PrL2DramDirectoryMSI::ShmemMsg::DRAM_READ_REP,
                  MemComponent::DRAM, MemComponent::TAG_DIR,
                  shmem_msg->getRequester(), /* requester */
                  tag_dir,           /* receiver */
                  shmem_msg->getAddress(), data_buf,
                  getCacheBlockSize(), 
                  shmem_msg->getWhere(),
                  shmem_msg->getPerf(), ShmemPerfModel::_SIM_THREAD);

         break;
      }

      case PrL1PrL2DramDirectoryMSI::ShmemMsg::CXL_VN_UPDATE_REP:
      {
         IntPtr address = shmem_msg->getAddress();
         MYLOG("[dram cntlr %d] handle CXL VN UPDATE REP @%016lx\n", getMemoryManager()->getCore()->getId(), address);
         /* gen MAC, encrypt and write to DRAM */
         handleVNUpdateFromCXL(address, shmem_msg->getRequester(), shmem_msg->getDataBuf(), msg_time);
         break;
      }

      default:
         LOG_PRINT_ERROR("Unrecognized Shmem Msg Type: %u", shmem_msg_type);
         break;
   }
}