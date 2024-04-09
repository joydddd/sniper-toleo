#include "queue_model_windowed_mg2.h"
#include "simulator.h"
#include "config.hpp"
#include "log.h"
#include "stats.h"

QueueModelWindowedMG2::QueueModelWindowedMG2(String name, UInt32 id, SubsecondTime min_processing_time)
   : m_window_size(SubsecondTime::NS(Sim()->getCfg()->getInt("queue_model/windowed_mg2/window_size")))
   , max_util(1.)
   , m_total_requests(0)
   , m_total_utilized_time(SubsecondTime::Zero())
   , m_total_queue_delay(SubsecondTime::Zero())
   , m_num_arrivals(0)
   , m_service_time_sum(0)
   , m_service_time_sum2(0)
{
   SubsecondTime quantum_barrier = SubsecondTime::NS(Sim()->getCfg()->getInt("clock_skew_minimization/barrier/quantum")); // default 100
   float feedback_fc = Sim()->getCfg()->getFloat("queue_model/windowed_mg2/feedback_fc"); // default 2.5
   max_util = 1. - (float)min_processing_time.getPS() / quantum_barrier.getPS() / feedback_fc;
   registerStatsMetric(name, id, "num-requests", &m_total_requests);
   registerStatsMetric(name, id, "total-time-used", &m_total_utilized_time);
   registerStatsMetric(name, id, "total-queue-delay", &m_total_queue_delay);
}

QueueModelWindowedMG2::~QueueModelWindowedMG2()
{}

SubsecondTime
QueueModelWindowedMG2::computeQueueDelay(SubsecondTime pkt_time, SubsecondTime processing_time, core_id_t requester)
{
   SubsecondTime t_queue = SubsecondTime::Zero();

   // Advance the window based on the global (barrier) time, as this guarantees the earliest time any thread may be at.
   // Use a backup value of 10 window sizes before the current request to avoid excessive memory usage in case something fishy is going on.
   removeItems(std::max(Sim()->getClockSkewMinimizationServer()->getGlobalTime() - m_window_size, pkt_time - 10*m_window_size));

   if (m_num_arrivals > 1)
   {
      double utilization = (double)m_service_time_sum / m_window_size.getPS();
      double arrival_rate = (double)m_num_arrivals / m_window_size.getPS();

      double service_time_Es2 = m_service_time_sum2 / m_num_arrivals;

      // If requesters do not throttle based on returned latency, it's their problem, not ours
      if (utilization > max_util)
         utilization = max_util;

      t_queue = SubsecondTime::PS(arrival_rate * service_time_Es2 / (2 * (1. - utilization)));

      // Our memory is limited in time to m_window_size. It would be strange to return more latency than that.
      if (t_queue > m_window_size)
         t_queue = m_window_size;
   }

   addItem(pkt_time, processing_time);

   m_total_requests++;
   m_total_utilized_time += processing_time;
   m_total_queue_delay += t_queue;

   return t_queue;
}

void
QueueModelWindowedMG2::addItem(SubsecondTime pkt_time, SubsecondTime service_time)
{
   m_window.insert(std::pair<SubsecondTime, SubsecondTime>(pkt_time, service_time));
   m_num_arrivals ++;
   m_service_time_sum += service_time.getPS();
   m_service_time_sum2 += service_time.getPS() * service_time.getPS();
}

void
QueueModelWindowedMG2::removeItems(SubsecondTime earliest_time)
{
   while(!m_window.empty() && m_window.begin()->first < earliest_time)
   {
      std::multimap<SubsecondTime, SubsecondTime>::iterator entry = m_window.begin();
      m_num_arrivals --;
      m_service_time_sum -= entry->second.getPS();
      m_service_time_sum2 -= entry->second.getPS() * entry->second.getPS();
      m_window.erase(entry);
   }
}
