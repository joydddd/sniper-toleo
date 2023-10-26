#ifndef __CXL_ADDRESS_TRANSLATION_H__
#define __CXL_ADDRESS_TRANSLATION_H__

#include <vector>
#include <unordered_map>
#include <random>

#include "fixed_types.h"
#include "lock.h"


/* 
   Does the job of Page Table and TLB, maps address from virtual address space into physical address space. 
*/

class CXLAddressTranslator
{
   public:
      CXLAddressTranslator(std::vector<UInt64> cxl_memory_expander_size, std::vector<core_id_t> cxl_cntlr_core_list, UInt32 page_size, UInt32 cacheline_size, UInt64 dram_total_size);
      ~CXLAddressTranslator();
      // Return cxl node for a given address
      cxl_id_t getHome(IntPtr address);
      // Return core node with cxl cntlr for given CXL device
      core_id_t getCntlrHome(cxl_id_t cxl_id);
      // Within cxl node, return unique, incrementing block number
      IntPtr getLinearPage(IntPtr address);
      // Within cxl node, return unique, incrementing address to be used in cache set selection
      IntPtr getPhyAddress(IntPtr address);

      UInt32 getnumCXLDevices() { return m_num_cxl_devs; }

      void printPageUsage();
      void printPageTable();

      // generated (physical) MAC address for a given virtual address. Assigned to dedicated MAC region. 
      IntPtr getMACAddrFromVirtual(IntPtr virtual_address); 
      IntPtr getMACAddrFromPhysical(IntPtr phy_addr, cxl_id_t cxl_id);
   

     private:

      UInt32 m_num_cxl_devs;
      UInt64 m_dram_size; // in Bytes
      UInt32 m_page_size; // in Bytes
      UInt32 m_page_offset;
      Lock pt_lock;

      bool m_has_mac; // in Bytes
      UInt32 m_mac_per_cl; // number of MACs to fit in one cacheline 
      UInt32 m_cacheline_size; // in Bytes
      std::vector<IntPtr> m_mac_offset; // address offset for mac. index with cxl_id, host index with m_num_cxl_devs

      std::unordered_map<IntPtr, IntPtr> m_addr_map; // mapping Virtual Page Number to CXL Node + Linear Page Number
      std::vector<UInt64> m_cxl_dev_size; // in Bytes
      UInt64* m_num_allocated_pages;
      std::vector<core_id_t> m_cxl_cntlr_core_list;

      std::vector<float> m_page_distribution;

      IntPtr allocatePage(IntPtr vpn); // return physical page number
      IntPtr getPPN(IntPtr vpn);

      cxl_id_t m_last_allocated; // home mapped to m_num_cxl_devs
      FILE* f_page_table;
};

#endif /* __CXL_ADDRESS_TRANSLATION_H__ */