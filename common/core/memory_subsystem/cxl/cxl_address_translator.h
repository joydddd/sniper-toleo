#ifndef __CXL_ADDRESS_TRANSLATION_H__
#define __CXL_ADDRESS_TRANSLATION_H__

#include <vector>
#include <map>

#include "fixed_types.h"


/* 
   Does the job of Page Table and TLB, maps address from virtual address space into physical address space. 
*/

class CXLAddressTranslator
{
   public:
      CXLAddressTranslator(std::vector<UInt64>& cxl_memory_expander_size, std::vector<core_id_t>& cxl_cntlr_core_list, UInt32 m_page_size, UInt64 dram_total_size);
      ~CXLAddressTranslator();
      // Return cxl node for a given address
      cxl_id_t getHome(IntPtr address);
      // Return core node with cxl cntlr for given CXL device
      core_id_t getCntlrHome(cxl_id_t cxl_id);
      // Within cxl node, return unique, incrementing block number
      IntPtr getLinearPage(IntPtr address);
      // Within cxl node, return unique, incrementing address to be used in cache set selection
      IntPtr getLinearAddress(IntPtr address);

   private:
      UInt32 m_num_cxl_devs;
      UInt64 m_dram_size;
      UInt32 m_page_size;
      UInt32 m_page_offset;

      std::map<IntPtr, IntPtr> m_addr_map; // mapping Virtual Page Number to CXL Node + Linear Page Number
      std::vector<UInt64> m_cxl_dev_size;
      std::vector<UInt64> m_num_allocated_pages;
      std::vector<core_id_t> m_cxl_cntlr_core_list;

      std::vector<float> m_memory_portion;

      IntPtr allocatePage(IntPtr vpn); // return physical page number
      IntPtr getPPN(IntPtr address);

      cxl_id_t m_last_allocated;
      FILE* f_page_table;
};

#endif /* __CXL_ADDRESS_TRANSLATION_H__ */