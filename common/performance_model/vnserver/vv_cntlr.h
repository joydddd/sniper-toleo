#ifndef __VV_CNTLR_H__
#define __VV_CNTLR_H__

#include "fixed_types.h"
#include "boost/tuple/tuple.hpp"
#include "simulator.h"
#include "config.h"
#include "config.hpp"
#include "cxl_address_translator.h"

#include <unordered_map>

#define VN_ENTRY_SIZE 16 // in bytes

class VVCntlr;

class VN_Page {
    public:
    typedef enum { ONE_STEP = 16, VAULT = 128, OVERFLOW = 512 } vn_comp_t;

    VN_Page(){}
    ~VN_Page(){}

    vn_comp_t update(UInt8 cl_num, VVCntlr* vn_cntlr); // update Vault Page entry type
    vn_comp_t get_type() { return type; }
    IntPtr get_entry_ptr() { return m_entry_ptr; }
    UInt32 reset(); // return number of CLs to re-encrypt

    private:
    vn_comp_t type = ONE_STEP;
    UInt8 vn_private[64] = {0};
    IntPtr m_entry_ptr = NULL;
    UInt32 m_total_offset = 0;  // sum of vn_private.
};

class VVCntlr {
    private:
    CXLAddressTranslator* m_cxl_addr_translator;

    std::unordered_map<IntPtr, VN_Page*> m_vault_pages;
    IntPtr m_vault_entry_offset, m_overflow_entry_offset;
    IntPtr m_vault_entry_tail, m_overflow_entry_tail;

    IntPtr allocateVNEntry(VN_Page::vn_comp_t type);

    friend class VN_Page;

   public:
    VVCntlr(CXLAddressTranslator* m_cxl_addr_translator);
    ~VVCntlr();

    /* return page type + ONE-STEP entry addr + (optional) vault/overflow entry index */
    boost::tuple<VN_Page::vn_comp_t, IntPtr, IntPtr> getVNEntry(IntPtr v_addr);

    /* return page type + ONE-STEP entry addr / (if pointing to a vault/overflow entry) vault/overflow entry index */
    boost::tuple<VN_Page::vn_comp_t, IntPtr> updateVNEntry(IntPtr v_addr);

    void printVVUsage();
};

#endif /* __VV_CNTLR_H__*/