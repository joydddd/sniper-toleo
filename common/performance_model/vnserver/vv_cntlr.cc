#include "vv_cntlr.h"
#include "log.h"


#define VAULT_PRIVATE_MAX 127
#define PAGE_OFFSET 12 // 4KB page (64 CLs)
#define PAGE_SIZE 4096 // 4KB page (64 CLs)
#define CACHE_LINE_OFFSET 6 // 64B cache line

#define oneK 1024
#define oneM (oneK * 1024)
#define oneG (oneM * 1024)

VN_Page::vn_comp_t VN_Page::update(UInt8 cl_num, VVCntlr* vn_cntlr){
    switch (type){
        case ONE_STEP:
            ++vn_private[cl_num];
            if (vn_private[cl_num] > 1) {
                type = VAULT;
                m_entry_ptr = vn_cntlr->allocateVNEntry(VAULT);
            }
            break;
        case VAULT:
            ++vn_private[cl_num];
            if (vn_private[cl_num] >= VAULT_PRIVATE_MAX) {
                type = OVERFLOW;
                m_entry_ptr = vn_cntlr->allocateVNEntry(OVERFLOW);
            }
            break;
        case OVERFLOW:
            ++vn_private[cl_num];
            break;
    }
    m_total_offset++;
    if (type == ONE_STEP && m_total_offset == 64) { // rollover one step pages if they are uniform now
        m_total_offset = 0;
        for (int i = 0; i < 64; i++) vn_private[i] = 0;
    }
    return type;
}

UInt32 VN_Page::reset(){
    UInt32 ret = m_total_offset;
    
    if (type == ONE_STEP) {
        ret = m_total_offset == 0 ? 0 : 64 - m_total_offset; // pages that are not written to need a forced update
    }
    if (type == VAULT || type == OVERFLOW) {
        ret = 64;
        return 64;
    }

    m_total_offset = 0;
    m_entry_ptr = NULL;
    type = ONE_STEP;
    for (int i = 0; i < 64; i++) vn_private[i] = 0;
    return ret;
}

VVCntlr::VVCntlr(CXLAddressTranslator* m_cxl_addr_translator):
    m_vault_entry_offset(m_cxl_addr_translator->getTotalPageCount() * VN_ENTRY_SIZE),
    m_overflow_entry_offset(m_vault_entry_offset + Sim()->getCfg()->getInt("perf_model/cxl/vnserver/vault_entry_size") * oneG),
    m_vault_entry_tail(0),
    m_overflow_entry_tail(0){}
    

VVCntlr::~VVCntlr(){
    for (auto it = m_vault_pages.begin(); it != m_vault_pages.end(); it++)
        delete it->second;
    m_vault_pages.clear();
}

IntPtr VVCntlr::allocateVNEntry(VN_Page::vn_comp_t type){
    switch(type){
        case VN_Page::ONE_STEP:
            LOG_PRINT_ERROR("[VVCntlr] Cannot allocate one step entry. ");
        case VN_Page::VAULT:
            return m_vault_entry_offset + (m_vault_entry_tail += (UInt64) VN_Page::VAULT);
        case VN_Page::OVERFLOW:
            return m_overflow_entry_offset + (m_overflow_entry_tail += (UInt64) VN_Page::OVERFLOW);
        default:
            LOG_PRINT_ERROR("[VVCntlr] Unknown VN_Page type %d. ", type);
    }
}


/* return page type + ONE-STEP entry addr + entry index */
boost::tuple<VN_Page::vn_comp_t, IntPtr, IntPtr> VVCntlr::getVNEntry(IntPtr address){
    // get Vault Page Type
    VN_Page::vn_comp_t vn_page_type = VN_Page::vn_comp_t::ONE_STEP;
    IntPtr page_num = address >> PAGE_OFFSET;
    UInt8 cl_num = (address & (PAGE_SIZE - 1)) >> CACHE_LINE_OFFSET;
    auto page_it = m_vault_pages.find(page_num);
    if (page_it != m_vault_pages.end()) { // if page exists. 
       vn_page_type = page_it->second->get_type();
    }

    return boost::make_tuple<VN_Page::vn_comp_t, IntPtr>(
        vn_page_type, 
        page_num * VN_ENTRY_SIZE,
        vn_page_type == VN_Page::vn_comp_t::ONE_STEP
            ? page_num * VN_ENTRY_SIZE
            : page_it->second->get_entry_ptr());
}

boost::tuple<VN_Page::vn_comp_t, IntPtr> VVCntlr::updateVNEntry(IntPtr address){
    IntPtr page_num = address >> PAGE_OFFSET;
    UInt8 cl_num = (address & (PAGE_SIZE - 1)) >> CACHE_LINE_OFFSET;

    // If page doesn't exist, allocate it 
    auto page_it = m_vault_pages.find(page_num);
    if (page_it == m_vault_pages.end()) {
        page_it = m_vault_pages.insert(std::make_pair(page_num, new VN_Page())).first;
    }

    // update the page
    VN_Page::vn_comp_t new_page_type = page_it->second->update(cl_num, this);
    return boost::make_tuple<VN_Page::vn_comp_t, IntPtr>(new_page_type, page_it->second->get_entry_ptr());
}