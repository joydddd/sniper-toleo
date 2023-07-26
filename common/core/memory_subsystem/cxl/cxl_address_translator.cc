#include "cxl_address_translator.h"
#include "log.h"
#include "simulator.h"
#include "stats.h"
#include <cmath>
#include <cstring>
#include <random>
#include "config.h"
#include "config.hpp"

#define MAX_PAGE_NUM_BITS 56


// DEBUG:
#if 0
#  define MYLOG_ENABLED
   extern Lock iolock;
#  include "core_manager.h"
#  include "simulator.h"
#  define MYLOG(...) {                                                                    \
   ScopedLock l(iolock);                                                                  \
   fflush(f_page_table);                                                                  \
   fprintf(f_page_table, "%cdr %-25s@%3u: ",                                \
         Sim()->getCoreManager()->amiUserThread() ? '^' : '_', __FUNCTION__, __LINE__);   \
   fprintf(f_page_table, __VA_ARGS__); fprintf(f_page_table, "\n"); fflush(f_page_table); }
#else
#  define MYLOG(...) {}
#endif

CXLAddressTranslator::CXLAddressTranslator(
      std::vector<UInt64> cxl_memory_expander_size,
      std::vector<core_id_t> cxl_cntlr_core_list, UInt32 page_size, UInt32 cacheline_size,
      UInt64 dram_total_size):
      m_num_cxl_devs(cxl_memory_expander_size.size()),
      m_dram_size(dram_total_size),
      m_page_size(page_size),
      m_page_offset(log2(page_size)),
      m_mac_size_per_page(0),
      m_cxl_dev_size(cxl_memory_expander_size),
      m_num_allocated_pages(NULL),
      m_cxl_cntlr_core_list(cxl_cntlr_core_list),
      m_last_allocated(0),
      f_page_table(NULL)
{
   // Each Block Address is as follows:
   // ////////////////////////////////////////////////////////////////////////////////////////////////////
   //  63 - 56 |
   //  cxl_node_id  (HOST_CXL_ID for dram)          |  page_num //
   // ////////////////////////////////////////////////////////////////////////////////////////////////////

   LOG_ASSERT_ERROR((1 << m_page_offset) == (SInt32)m_page_size,
                  "Page Size %u is not a 2^N", m_page_size);

   UInt64 total_memory_size = m_dram_size;
   for (auto it = m_cxl_dev_size.begin(); it != m_cxl_dev_size.end();
      ++it) {
      total_memory_size += *it;
   }

   m_memory_portion.resize(m_num_cxl_devs + 1);
   float accumuated_portion = 0;
   for (unsigned int i = 0; i < m_num_cxl_devs; i++) {
      m_memory_portion[i] = (float)m_cxl_dev_size[i] / total_memory_size + accumuated_portion;
      accumuated_portion = m_memory_portion[i] ;
   }
   m_memory_portion[m_num_cxl_devs] = 1.0;


   f_page_table = fopen("page_table.log", "w+");

   // Remap MAC address to the end of each page. 
   m_has_mac = Sim()->getCfg()->getBool("perf_model/mee/enable"); // if allocate extra space for mac. 
   if (m_has_mac) {
      m_mac_size_per_page = Sim()->getCfg()->getInt("perf_model/mee/mac_length") ; // mac_length in bits
      m_mac_size_per_page = (m_mac_size_per_page * (page_size / cacheline_size)) / 8; // covert to bytes per page
   }

   // Allocate arrays
   m_num_allocated_pages = (UInt64*)malloc(sizeof(UInt64) * (m_num_cxl_devs + 1));
   memset(m_num_allocated_pages, 0, sizeof(UInt64) * (m_num_cxl_devs + 1));

   // register stats for page mapping 
   for (cxl_id_t cxl_id = 0; cxl_id < m_num_cxl_devs; ++cxl_id){
      registerStatsMetric("page_table", cxl_id, "pages", &m_num_allocated_pages[cxl_id]);
   }
   registerStatsMetric("page_table", m_num_cxl_devs, "pages", &m_num_allocated_pages[m_num_cxl_devs]);
   
}

IntPtr CXLAddressTranslator::getMACaddr(IntPtr address){
   static UInt32 mac_size = Sim()->getCfg()->getInt("perf_model/mee/mac_length"); // in bits
   static UInt32 cache_block_size = Sim()->getCfg()->getInt("perf_model/l2_cache/cache_block_size"); // in bytes
   static UInt32 mac_per_cacheline = (cache_block_size * 8) / mac_size;
   static UInt32 page_offset = Sim()->getCfg()->getInt("system/addr_trans/page_offset");
   IntPtr vpn = address >> page_offset;
   IntPtr mac_addr = (address & (((IntPtr)1 << page_offset) - 1)) / mac_per_cacheline;
   mac_addr = (mac_addr / cache_block_size) * cache_block_size; // round down to cacheline boundary
   mac_addr = MAC_MARK | (vpn << page_offset) | mac_addr;
   // fprintf(stderr, "addr: 0x%016lx -> mac_addr: 0x%016lx\n", address, mac_addr);
   LOG_ASSERT_ERROR( (MAC_MASK & address) == 0, "MAC address 0x%016lx exceeds the limit 0x%016lx", mac_addr, MAC_MASK);
   return mac_addr;
}

CXLAddressTranslator::~CXLAddressTranslator()
{
   if (f_page_table){
      MYLOG("======================================= END ============================================");
      printPageUsage();
      printPageTable();
      fclose(f_page_table);
   }
}

cxl_id_t CXLAddressTranslator::getHome(IntPtr address)
{
   cxl_id_t cxl_id = getPPN(address) >> MAX_PAGE_NUM_BITS;
   MYLOG("0x%016lx -> cxl_id: %d", address, cxl_id);
   return cxl_id;
}

core_id_t CXLAddressTranslator::getCntlrHome(cxl_id_t cxl_id)
{
   MYLOG("[cxl_id]%d -> [core_id]%u", cxl_id, m_cxl_cntlr_core_list[cxl_id]);
   LOG_ASSERT_ERROR(cxl_id < m_num_cxl_devs, "Invalid cxl id %u", cxl_id);
   return m_cxl_cntlr_core_list[cxl_id];
}

IntPtr CXLAddressTranslator::getLinearPage(IntPtr address)
{
   return getPPN(address) & (((IntPtr)1 << MAX_PAGE_NUM_BITS) - 1);
}

IntPtr CXLAddressTranslator::getLinearAddress(IntPtr address)
{
   IntPtr linear_addr; 
   if ((address & MAC_MASK) == MAC_MARK){ // mac_address
      LOG_ASSERT_ERROR(m_has_mac, "address 0x%016lx is a mac address but mac is not enabled", address);
      LOG_ASSERT_ERROR((address & (((IntPtr)1 << m_page_offset) - 1)) < m_mac_size_per_page, "mac_addr 0x%016lx has offset larger than mac_size_per_page", address);
      linear_addr = getLinearPage(address) * (m_page_size + m_mac_size_per_page) +
             m_page_size + (address & (((IntPtr)1 << m_page_offset) - 1));
   } else {
      if (m_has_mac){
         linear_addr =  getLinearPage(address) * (m_page_size + m_mac_size_per_page) + 
         (address & (((IntPtr)1 << m_page_offset) - 1));
      } else {
         linear_addr =  getLinearPage(address) << m_page_offset | (address & (((IntPtr)1 << m_page_offset) - 1));
      }
   }
   MYLOG("Linear addr: 0x%016lx -> 0x%016lx\n", address, linear_addr);
   return linear_addr;
}

IntPtr CXLAddressTranslator::getPPN(IntPtr address)
{
   IntPtr vpn = (m_has_mac ? (address & ~(MAC_MASK)) : address ) >> m_page_offset;
   IntPtr ppn = 0;
   auto it_vpn = m_addr_map.find(vpn);
   if (it_vpn == m_addr_map.end()){
      // page have never been accessed before
      LOG_ASSERT_ERROR( (address & MAC_MASK) != MAC_MARK, "address 0x%016lx is a mac address but page has never been allocated", address)
      ppn = allocatePage(vpn);
   } else {
      ppn = it_vpn->second;
   }
   return ppn;
}

/* TODO: smartly decide whether a page is located on cxl or host dram */

/* Round Robin allocation */
// IntPtr CXLAddressTranslator::allocatePage(IntPtr vpn){
//    m_last_allocated = (m_last_allocated + 1) % (m_num_cxl_devs + 1);
//    ++m_num_allocated_pages[m_last_allocated];

//    LOG_ASSERT_ERROR(m_num_allocated_pages[m_last_allocated] < ((IntPtr)1 << MAX_PAGE_NUM_BITS),
//          "Number of pages allocated to cxl node %u exceeds the limit %u",
//          m_last_allocated, ((IntPtr)1 << MAX_PAGE_NUM_BITS) - 1);

//    IntPtr ppn = (UInt64)(m_last_allocated >= m_num_cxl_devs ? HOST_CXL_ID : m_last_allocated) << MAX_PAGE_NUM_BITS | m_num_allocated_pages[m_last_allocated];
//    m_addr_map[vpn] = ppn;
//    MYLOG("[vpn]0x%016lx -> [ppn]0x%016lx", vpn, ppn);
//    printPageUsage();
//    return ppn;
// }

/* Random Allocation w.r.t to size. */
IntPtr CXLAddressTranslator::allocatePage(IntPtr vpn){
   float rand_loc = (float)rand() / (float)RAND_MAX;
   for (m_last_allocated = 0; m_last_allocated < m_num_cxl_devs + 1; m_last_allocated++){
      if (m_memory_portion[m_last_allocated] >= rand_loc){
         break;
      }
   }
   ++m_num_allocated_pages[m_last_allocated];

   LOG_ASSERT_ERROR(m_num_allocated_pages[m_last_allocated] < ((IntPtr)1 << MAX_PAGE_NUM_BITS),
         "Number of pages allocated to cxl node %u exceeds the limit %u",
         m_last_allocated, ((IntPtr)1 << MAX_PAGE_NUM_BITS) - 1);

   IntPtr ppn = (UInt64)(m_last_allocated >= m_num_cxl_devs ? HOST_CXL_ID : m_last_allocated) << MAX_PAGE_NUM_BITS | m_num_allocated_pages[m_last_allocated];
   m_addr_map[vpn] = ppn;
   MYLOG("[vpn]0x%016lx -> [ppn]0x%016lx", vpn, ppn);
#ifdef MYLOG_ENABLED
   printPageUsage();
#endif // MYLOG_ENABLED
   return ppn;
}

void CXLAddressTranslator::printPageUsage(){
   fprintf(f_page_table,"Page Usage:\n");
   for (unsigned int i = 0; i < m_num_cxl_devs; i++) {
      fprintf(f_page_table, "\tCXL Node %u: %lu pages %.4f MB / %.2f GB\n", i, m_num_allocated_pages[i],  m_num_allocated_pages[i] * m_page_size /((float)1000*1000), m_cxl_dev_size[i]/((float)1000*1000*1000));
   }
   fprintf(f_page_table, "\tDRAM: %lu pages %.4f MB / %.2f GB\n", m_num_allocated_pages[m_num_cxl_devs],  m_num_allocated_pages[m_num_cxl_devs] * m_page_size /((float)1000*1000), m_dram_size/((float)1000*1000*1000));
}


void CXLAddressTranslator::printPageTable(){
   fprintf(f_page_table, "Page Table:\n");
   for (auto it = m_addr_map.begin(); it != m_addr_map.end(); it++){
      fprintf(f_page_table, "\t0x%016lx -> 0x%016lx\n", it->first, it->second);
   }
   fprintf(f_page_table, "=====================================\n");
}