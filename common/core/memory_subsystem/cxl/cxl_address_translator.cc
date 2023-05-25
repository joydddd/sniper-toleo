#include "cxl_address_translator.h"
#include "log.h"
#include "simulator.h"
#include <cmath>
#include <random>

#define MAX_PAGE_NUM_BITS 56


#if 1
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
      std::vector<UInt64>& cxl_memory_expander_size,
      std::vector<core_id_t>& cxl_cntlr_core_list, UInt32 m_page_size,
      UInt64 dram_total_size):
      m_num_cxl_devs(cxl_memory_expander_size.size()),
      m_dram_size(dram_total_size),
      m_page_size(m_page_size),
      m_page_offset(log2(m_page_size)),
      m_cxl_dev_size(cxl_memory_expander_size),
      m_num_allocated_pages(cxl_memory_expander_size.size(), 0),
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
       for (unsigned int i = 0; i < m_num_cxl_devs; i++) {
           m_memory_portion[i] = (float)m_cxl_dev_size[i] / total_memory_size;
       }
#ifdef MYLOG_ENABLED
   f_page_table = fopen("log_page_table", "w+");
#endif //mylog_enabled
}

CXLAddressTranslator::~CXLAddressTranslator()
{
   // There is no memory to deallocate, so destructor has no function
#ifdef MYLOG_ENABLED
   if (f_page_table)fclose(f_page_table);
#endif //mylog_enabled 
}

cxl_id_t CXLAddressTranslator::getHome(IntPtr address)
{
   return getPPN(address) >> MAX_PAGE_NUM_BITS;
}

core_id_t CXLAddressTranslator::getCntlrHome(cxl_id_t cxl_id)
{
   return m_cxl_cntlr_core_list[cxl_id];
}

IntPtr CXLAddressTranslator::getLinearPage(IntPtr address)
{
   return getPPN(address) & (((IntPtr)1 << MAX_PAGE_NUM_BITS) - 1);
}

IntPtr CXLAddressTranslator::getLinearAddress(IntPtr address)
{
   return getLinearPage(address) << m_page_offset | (address & (((IntPtr)1 << m_page_offset) - 1));
}

IntPtr CXLAddressTranslator::getPPN(IntPtr address)
{
   // virtual page number 
   IntPtr vpn = address >> m_page_offset;
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


IntPtr CXLAddressTranslator::allocatePage(IntPtr vpn){
   /* TODO: smartly decide whether a page is located on cxl or host dram */
   m_last_allocated = (m_last_allocated + 1) % (m_last_allocated + 1);
   m_num_allocated_pages[m_last_allocated] += 1;

   LOG_ASSERT_ERROR(m_num_allocated_pages[m_last_allocated] < ((IntPtr)1 << MAX_PAGE_NUM_BITS),
         "Number of pages allocated to cxl node %u exceeds the limit %u",
         m_last_allocated, ((IntPtr)1 << MAX_PAGE_NUM_BITS) - 1);

   IntPtr ppn = (UInt64)m_last_allocated << MAX_PAGE_NUM_BITS | m_num_allocated_pages[m_last_allocated];
   m_addr_map[vpn] = ppn;
   MYLOG("%08lx -> %8d %08lx", vpn, m_last_allocated, ppn);
   return ppn;
}