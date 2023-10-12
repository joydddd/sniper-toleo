#ifndef __CXL_PERF_MODEL_H__
#define __CXL_PERF_MODEL_H__

#include "queue_model.h"
#include "fixed_types.h"
#include "subsecond_time.h"
#include "cxl_cntlr_interface.h"

class ShmemPerf;

class CXLPerfModel
{
   protected:
      bool m_enabled;
      UInt64 m_num_accesses;
      UInt64 m_transaction_size_bytes;

      cxl_id_t m_cxl_id;
      FILE* f_trace;


     public:
      static CXLPerfModel* createCXLPerfModel(cxl_id_t cxl_id, UInt64 transaction_size, bool is_vn = false);
      CXLPerfModel(cxl_id_t cxl_id, UInt64 transaction_size /* in bits */) : m_enabled(false), m_num_accesses(0),  m_transaction_size_bytes(transaction_size / 8), m_cxl_id(cxl_id) {}
      virtual ~CXLPerfModel() {}
      virtual SubsecondTime getAccessLatency(
          SubsecondTime pkt_time, UInt64 pkt_size, core_id_t requester,
          IntPtr address, CXLCntlrInterface::access_t access_type,
          ShmemPerf* perf) = 0;
      virtual void enable() { m_enabled = true; }
      virtual void disable() { m_enabled = false; }

      UInt64 getTotalAccesses() { return m_num_accesses; }
};

#endif // __CXL_PERF_MODEL_H__