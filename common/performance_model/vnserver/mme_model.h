#ifndef __MME_MODEL_H__
#define __MME_MODEL_H__

#include "subsecond_time.h"
#include "dram_cntlr_interface.h"
#include "fixed_types.h"

class mmeModel {
    protected:
     bool m_enabled;
     UInt64 m_num_reads;
     UInt64 m_num_writes;
    public:
     static mmeModel* createMMEModel(core_id_t core_id, UInt32 cache_block_size);

     mmeModel(core_id_t core_id, UInt64 cache_block_size) : m_enabled(false), m_num_reads(0), m_num_writes(0) {}
     virtual ~mmeModel() {}
     void enable() { m_enabled = true; }
     void disable() {m_enabled = false; }

     UInt64 getTotalReads() { return m_num_reads; }
     UInt64 getTotalWrites() { return m_num_writes; }
};

#endif /* __MME_MODEL_H__ */