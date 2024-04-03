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

#define ONE_G ((float)1000*1000*1000)
#define ONE_M  ((float)1000*1000)

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
      m_page_size(page_size), // in bytes, usually = 4K
      m_page_offset(log2(page_size)), // in bits, usually = 12
      m_mac_per_cl(0),
      m_cacheline_size(cacheline_size),
      m_mac_offset(m_num_cxl_devs+1, 0),
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

   m_page_distribution.resize(m_num_cxl_devs + 1);
   float accumuated_portion = 0;

   // Calculate page distribution w.r.t size
   // UInt64 total_memory_size = m_dram_size;
   // for (auto it = m_cxl_dev_size.begin(); it != m_cxl_dev_size.end();
   //    ++it) {
   //    total_memory_size += *it;
   // }
   // for (unsigned int i = 0; i < m_num_cxl_devs; i++) {
   //    m_page_distribution[i] = (float)m_cxl_dev_size[i] / total_memory_size + accumuated_portion;
   //    accumuated_portion = m_page_distribution[i] ;
   // }

   // Calculate page distribution w.r.t bandwidth
   float dram_bandwidth =
       Sim()->getCfg()->getFloat("perf_model/dram/per_controller_bandwidth") *
       Sim()->getCfg()->getInt("perf_model/dram/num_controllers");
   std::vector<float> cxl_bandwidth(m_num_cxl_devs);
   float mem_total_bw = dram_bandwidth;
   for (unsigned int i = 0; i < m_num_cxl_devs; i++){
      cxl_bandwidth[i] = Sim()->getCfg()->getFloat("perf_model/cxl/memory_expander_"+ itostr((unsigned int)i) +"/bandwidth");
      mem_total_bw += cxl_bandwidth[i];
   }
   for (unsigned int i = 0; i < m_num_cxl_devs; i++) {
      m_page_distribution[i] = cxl_bandwidth[i] / mem_total_bw + accumuated_portion;
      accumuated_portion = m_page_distribution[i] ;
   }
   m_page_distribution[m_num_cxl_devs] = 1.0;

   f_page_table = fopen("page_table.log", "w+");

   // Remap MAC address to a dedicated area: last 1/9 of address space. 
   m_has_mac = Sim()->getCfg()->getBool("perf_model/mee/enable")
         && Sim()->getCfg()->getString("perf_model/mee/type") == String("toleo")
         && Sim()->getCfg()->getBool("perf_model/mee/enable_mac"); // if allocate extra space for mac. 
   if (m_has_mac) {
      m_mac_per_cl = Sim()->getCfg()->getInt("perf_model/mee/mac_per_cl") ; // mac_length in bits
      UInt32 mac_size_per_page = (page_size / m_mac_per_cl); // covert to bytes per page
      for (cxl_id_t cxl_id = 0; cxl_id < m_num_cxl_devs; cxl_id++){
         UInt64 num_pages_per_dev = m_cxl_dev_size[cxl_id] / (m_page_size + mac_size_per_page);
         m_mac_offset[cxl_id] = num_pages_per_dev * m_page_size;
      }
      {// dram
         UInt64 num_pages_per_dev = m_dram_size / (m_page_size + mac_size_per_page);
         m_mac_offset[m_num_cxl_devs] = num_pages_per_dev * m_page_size;
      }

      fprintf(f_page_table,"Address Space breakdown for MAC + Salt: \n");
      for (cxl_id_t cxl_id = 0; cxl_id < m_num_cxl_devs; cxl_id++){
         fprintf(f_page_table, "CXL Dev %d %.2f GB: \n", cxl_id, m_cxl_dev_size[cxl_id] / ONE_G);
         fprintf(f_page_table, "\t\t\tdata 0x%12lx - 0x%12lx, %.2f GB\n", (unsigned long int)0, m_mac_offset[cxl_id], m_mac_offset[cxl_id] / ONE_G);
         fprintf(f_page_table, "\t\t\tmax+salt 0x%12lx - 0x%12lx, %.2f GB\n", m_mac_offset[cxl_id], m_cxl_dev_size[cxl_id], (m_cxl_dev_size[cxl_id] - m_mac_offset[cxl_id]) / ONE_G);
      }
      {
         fprintf(f_page_table, "DRAM Dev %.2f GB: \n", m_dram_size / ONE_G);
         fprintf(f_page_table, "\t\t\tdata 0x%12lx - 0x%12lx, %.2f GB\n", 0, m_mac_offset[m_num_cxl_devs], m_mac_offset[m_num_cxl_devs] / ONE_G);
         fprintf(f_page_table, "\t\t\tmax+salt 0x%12lx - 0x%12lx, %.2f GB\n", m_mac_offset[m_num_cxl_devs], m_dram_size, (m_dram_size - m_mac_offset[m_num_cxl_devs]) / ONE_G);
      }
   } else {
      for (cxl_id_t cxl_id = 0; cxl_id < m_num_cxl_devs; cxl_id++){
         m_mac_offset[cxl_id] = m_cxl_dev_size[cxl_id];
      }
      m_mac_offset[m_num_cxl_devs] = m_dram_size; // dram
   }

   // Allocate arrays
   m_num_allocated_pages =
           (UInt64*)malloc(sizeof(UInt64) * (m_num_cxl_devs + 1));
   memset(m_num_allocated_pages, 0, sizeof(UInt64) * (m_num_cxl_devs + 1));

   // register stats for page mapping 
   for (cxl_id_t cxl_id = 0; cxl_id < m_num_cxl_devs; ++cxl_id){
      registerStatsMetric("page_table", cxl_id, "pages", &m_num_allocated_pages[cxl_id]);
   }
   registerStatsMetric("page_table", m_num_cxl_devs, "pages", &m_num_allocated_pages[m_num_cxl_devs]);
   
}

IntPtr CXLAddressTranslator::getMACAddrFromVirtual(IntPtr virtual_address){
   IntPtr phy_addr = getPhyAddress(virtual_address);
   cxl_id_t cxl_id = getHome(virtual_address);
   return getMACAddrFromPhysical(phy_addr, cxl_id);
   
}

IntPtr CXLAddressTranslator::getMACAddrFromPhysical(IntPtr phy_addr, cxl_id_t cxl_id){
   LOG_ASSERT_ERROR(m_has_mac, "getMACaddr when mac is not enabled\n");

   IntPtr mac_addr = cxl_id == HOST_CXL_ID ? m_mac_offset[m_num_cxl_devs] : m_mac_offset[cxl_id];
   // Align physical address to cacheline * #mac/ CL boundary, and map to mac region
   mac_addr += phy_addr / (m_mac_per_cl * m_cacheline_size) * m_cacheline_size;

   LOG_ASSERT_ERROR((mac_addr & (m_cacheline_size - 1)) == 0,
                    "[CXL %d] physical addr 0x%12lx MAC addr 0x%12lx is "
                    "not aligned to cacheline (mac offset 0x%12lx)",
                     cxl_id, phy_addr, mac_addr, cxl_id == HOST_CXL_ID ? m_mac_offset[m_num_cxl_devs] : m_mac_offset[cxl_id]);
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
   cxl_id_t cxl_id = getPPN(address >> m_page_offset ) >> MAX_PAGE_NUM_BITS;
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
   return getPPN(address >> m_page_offset ) & (((IntPtr)1 << MAX_PAGE_NUM_BITS) - 1);
}

IntPtr CXLAddressTranslator::getPhyAddress(IntPtr virtual_address)
{
   IntPtr phy_addr; 
   phy_addr =  getLinearPage(virtual_address) << m_page_offset | (virtual_address & (((IntPtr)1 << m_page_offset) - 1));
   MYLOG("Linear addr: 0x%016lx -> 0x%016lx\n", virtual_address, phy_addr);
   return phy_addr;
}

IntPtr CXLAddressTranslator::getPPN(IntPtr vpn)
{
   ScopedLock l(pt_lock);
   IntPtr ppn = 0;
   auto it_vpn = m_addr_map.find(vpn);
   if (it_vpn == m_addr_map.end()){
      // page have never been accessed before
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

/* Random Allocation w.r.t to m_page_distribution. */
IntPtr CXLAddressTranslator::allocatePage(IntPtr vpn){
   float rand_loc = (float)rand() / (float)RAND_MAX;
   for (m_last_allocated = 0; m_last_allocated < m_num_cxl_devs + 1; m_last_allocated++){
      if (m_page_distribution[m_last_allocated] >= rand_loc){
         break;
      }
   }
   ++m_num_allocated_pages[m_last_allocated];

   LOG_ASSERT_ERROR(m_num_allocated_pages[m_last_allocated] < ((IntPtr)1 << MAX_PAGE_NUM_BITS),
         "Number of pages allocated to cxl node %u exceeds the limit %u",
         m_last_allocated, ((IntPtr)1 << MAX_PAGE_NUM_BITS) - 1);

   IntPtr ppn = (UInt64)(m_last_allocated >= m_num_cxl_devs ? HOST_CXL_ID : m_last_allocated) << MAX_PAGE_NUM_BITS | m_num_allocated_pages[m_last_allocated];
   m_addr_map[vpn] = ppn;
   fprintf(f_page_table, "[vpn]0x%016lx -> [ppn]0x%016lx\n", vpn, ppn);
#ifdef MYLOG_ENABLED
   printPageUsage();
#endif // MYLOG_ENABLED
   return ppn;
}


void CXLAddressTranslator::printPageUsage(){
   fprintf(f_page_table,"Page Usage:\n");
   for (unsigned int i = 0; i < m_num_cxl_devs; i++) {
      fprintf(f_page_table, "\tCXL Node %u: %lu pages %.4f MB / %.2f GB\n", i, m_num_allocated_pages[i],  m_num_allocated_pages[i] * m_page_size /((float)1000*1000), m_mac_offset[i]/((float)1000*1000*1000));
   }
   fprintf(f_page_table, "\tDRAM: %lu pages %.4f MB / %.2f GB\n", m_num_allocated_pages[m_num_cxl_devs],  m_num_allocated_pages[m_num_cxl_devs] * m_page_size /((float)1000*1000), m_mac_offset[m_num_cxl_devs]/((float)1000*1000*1000));
}


void CXLAddressTranslator::printPageTable(){
   fprintf(f_page_table, "Page Table:\n");
   for (auto it = m_addr_map.begin(); it != m_addr_map.end(); it++){
      fprintf(f_page_table, "\t0x%016lx -> 0x%016lx\n", it->first, it->second);
   }
   fprintf(f_page_table, "=====================================\n");
}