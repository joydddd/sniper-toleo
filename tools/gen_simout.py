#!/usr/bin/env python2

import sys, os, getopt, sniper_lib


def generate_simout(jobid = None, resultsdir = None, partial = None, output = sys.stdout, silent = False):

  try:
    res = sniper_lib.get_results(jobid = jobid, resultsdir = resultsdir, partial = partial)
  except (KeyError, ValueError), e:
    if not silent:
      print 'Failed to generated sim.out:', e
    return

  results = res['results']
  config = res['config']
  ncores = int(config['general/total_cores'])

  format_int = lambda v: str(long(v))
  format_pct = lambda v: '%.1f%%' % (100. * v)
  def format_float(digits):
    return lambda v: ('%%.%uf' % digits) % v
  def format_ns(digits):
    return lambda v: ('%%.%uf' % digits) % (v/1e6)

  if 'barrier.global_time_begin' in results:
    time0_begin = results['barrier.global_time_begin']
    time0_end = results['barrier.global_time_end']

  if 'barrier.global_time' in results:
    time0 = results['barrier.global_time'][0]
  else:
    time0 = time0_begin - time0_end

  if sum(results['performance_model.instruction_count']) == 0:
    # core.instructions is less exact, but in cache-only mode it's all there is
    results['performance_model.instruction_count'] = results['core.instructions']

  results['performance_model.elapsed_time_fixed'] = [
    time0
    for c in range(ncores)
  ]
  results['performance_model.cycle_count_fixed'] = [
    results['performance_model.elapsed_time_fixed'][c] * results['fs_to_cycles_cores'][c]
    for c in range(ncores)
  ]
  results['performance_model.ipc'] = [
    i / (c or 1)
    for i, c in zip(results['performance_model.instruction_count'], results['performance_model.cycle_count_fixed'])
  ]
  results['performance_model.nonidle_elapsed_time'] = [
    results['performance_model.elapsed_time'][c] - results['performance_model.idle_elapsed_time'][c]
    for c in range(ncores)
  ]
  results['performance_model.idle_elapsed_time'] = [
    time0 - results['performance_model.nonidle_elapsed_time'][c]
    for c in range(ncores)
  ]
  results['performance_model.idle_elapsed_percent'] = [
    results['performance_model.idle_elapsed_time'][c] / float(time0)
    for c in range(ncores)
  ]

  template = [
    ('  Instructions', 'performance_model.instruction_count', str),
    ('  Cycles',       'performance_model.cycle_count_fixed', format_int),
    ('  IPC',          'performance_model.ipc', format_float(2)),
    ('  Time (ns)',    'performance_model.elapsed_time_fixed', format_ns(0)),
    ('  Idle time (ns)', 'performance_model.idle_elapsed_time', format_ns(0)),
    ('  Idle time (%)',  'performance_model.idle_elapsed_percent', format_pct),
  ]

  if 'branch_predictor.num-incorrect' in results:
    results['branch_predictor.missrate'] = [ 100 * float(results['branch_predictor.num-incorrect'][core])
      / ((results['branch_predictor.num-correct'][core] + results['branch_predictor.num-incorrect'][core]) or 1) for core in range(ncores) ]
    results['branch_predictor.mpki'] = [ 1000 * float(results['branch_predictor.num-incorrect'][core])
      / (results['performance_model.instruction_count'][core] or 1) for core in range(ncores) ]
    template += [
      ('Branch predictor stats', '', ''),
      ('  num correct',  'branch_predictor.num-correct', str),
      ('  num incorrect','branch_predictor.num-incorrect', str),
      ('  misprediction rate', 'branch_predictor.missrate', lambda v: '%.2f%%' % v),
      ('  mpki', 'branch_predictor.mpki', lambda v: '%.2f' % v),
    ]

  template += [
    ('TLB Summary', '', ''),
  ]

  for tlb in ('itlb', 'dtlb', 'stlb'):
    if '%s.access'%tlb in results:
      results['%s.missrate'%tlb] = map(lambda (a,b): 100*a/float(b or 1), zip(results['%s.miss'%tlb], results['%s.access'%tlb]))
      results['%s.mpki'%tlb] = map(lambda (a,b): 1000*a/float(b or 1), zip(results['%s.miss'%tlb], results['performance_model.instruction_count']))
      template.extend([
        ('  %s' % {'itlb': 'I-TLB', 'dtlb': 'D-TLB', 'stlb': 'L2 TLB'}[tlb], '', ''),
        ('    num accesses', '%s.access'%tlb, str),
        ('    num misses', '%s.miss'%tlb, str),
        ('    miss rate', '%s.missrate'%tlb, lambda v: '%.2f%%' % v),
        ('    mpki', '%s.mpki'%tlb, lambda v: '%.2f' % v),
      ])

  template += [
    ('Cache Summary', '', ''),
  ]
  allcaches = [ 'L1-I', 'L1-D' ] + [ 'L%u'%l for l in range(2, 5) ]
  existcaches = [ c for c in allcaches if '%s.loads'%c in results ]
  for c in existcaches:
    results['%s.accesses'%c] = map(sum, zip(results['%s.loads'%c], results['%s.stores'%c]))
    results['%s.misses'%c] = map(sum, zip(results['%s.load-misses'%c], results.get('%s.store-misses-I'%c, results['%s.store-misses'%c])))
    results['%s.missrate'%c] = map(lambda (a,b): 100*a/float(b) if b else float('inf'), zip(results['%s.misses'%c], results['%s.accesses'%c]))
    results['%s.mpki'%c] = map(lambda (a,b): 1000*a/float(b) if b else float('inf'), zip(results['%s.misses'%c], results['performance_model.instruction_count']))
    template.extend([
      ('  Cache %s'%c, '', ''),
      ('    num cache accesses', '%s.accesses'%c, str),
      ('    num cache misses', '%s.misses'%c, str),
      ('    miss rate', '%s.missrate'%c, lambda v: '%.2f%%' % v),
      ('    mpki', '%s.mpki'%c, lambda v: '%.2f' % v),
    ])

  allcaches = [ 'nuca-cache', 'dram-cache' ]
  existcaches = [ c for c in allcaches if '%s.reads'%c in results ]
  for c in existcaches:
    results['%s.accesses'%c] = map(sum, zip(results['%s.reads'%c], results['%s.writes'%c]))
    results['%s.misses'%c] = map(sum, zip(results['%s.read-misses'%c], results['%s.write-misses'%c]))
    results['%s.missrate'%c] = map(lambda (a,b): 100*a/float(b) if b else float('inf'), zip(results['%s.misses'%c], results['%s.accesses'%c]))
    icount = sum(results['performance_model.instruction_count'])
    icount /= len([ v for v in results['%s.accesses'%c] if v ]) # Assume instructions are evenly divided over all cache slices
    results['%s.mpki'%c] = map(lambda a: 1000*a/float(icount) if icount else float('inf'), results['%s.misses'%c])
    template.extend([
      ('  %s cache'% c.split('-')[0].upper(), '', ''),
      ('    num cache accesses', '%s.accesses'%c, str),
      ('    num cache misses', '%s.misses'%c, str),
      ('    miss rate', '%s.missrate'%c, lambda v: '%.2f%%' % v),
      ('    mpki', '%s.mpki'%c, lambda v: '%.2f' % v),
    ])
    
  if 'page_table.pages' in results:
    results['page_table.usage'] = map(lambda a: float(config['system/addr_trans/page_size']) * a / (1024*1024), results['page_table.pages'])
    template += [
      ('Page Tabel summary', '', ''),
      ('  num pages [cxl, dram]', 'page_table.pages', str),
      ('  usage (GB) [cxl, dram]', 'page_table.usage', lambda v: '%.4f' % v),
    ]
    
  if 'dram.data-reads' in results:
    template += [
      ('DRAM effective Access', '', '')
    ]
    results['dram.data-accesses'] = map(sum, zip(results['dram.data-reads'], results['dram.data-writes']))
    results['dram.mac-accesses'] = map(sum, zip(results['dram.mac-reads'], results['dram.mac-writes']))
    results['dram.avgdatalatency'] = map(lambda (a,b): a/b if b else float('inf'), zip(results['dram.total-data-read-delay'], results['dram.data-reads']))
    results['dram.databw'] = map(lambda a: float(64*a)/(time0/1e6) if time0 else float('inf'), results['dram.data-accesses'])
    template.append(('  num data reads', 'dram.data-reads', str))
    template.append(('  num data writes', 'dram.data-writes', str))
    template.append(('  num mac accesses', 'dram.mac-accesses', str))
    template.append(('  average data read latency', 'dram.avgdatalatency', format_ns(2)))
    template.append(('  average data bandwidth (GB/s)', 'dram.databw', lambda v: '%.2f' % v))

  results['dram.accesses'] = map(sum, zip(results['dram.reads'], results['dram.writes']))
  results['dram.avglatency'] = map(lambda (a,b): a/b if b else float('inf'), zip(results['dram.total-access-latency'], results['dram.accesses']))
  template += [
    ('DRAM summary', '', ''),
    ('  num dram accesses', 'dram.accesses', str),
    ('  average dram access latency (ns)', 'dram.avglatency', format_ns(2)),
  ]
  if 'dram.total-read-latency' in results:
    results['dram.avgreadlat'] = map(lambda (a,b): a/b if b else float('inf'), zip(results['dram.total-read-latency'], results['dram.reads']))
    template.append(('  average dram read latency (ns)', 'dram.avgreadlat', format_ns(2)))
  if 'dram.total-read-queueing-delay' in results:
    results['dram.avgqueueread'] = map(lambda (a,b): a/(b or 1), zip(results['dram.total-read-queueing-delay'], results['dram.reads']))
    results['dram.avgqueuewrite'] = map(lambda (a,b): a/(b or 1), zip(results['dram.total-write-queueing-delay'], results['dram.writes']))
    template.append(('  average dram read queueing delay', 'dram.avgqueueread', format_ns(2)))
    template.append(('  average dram write queueing delay', 'dram.avgqueuewrite', format_ns(2)))
  elif 'dram.total-queueing-delay' in results:
    results['dram.avgqueue'] = map(lambda (a,b): a/(b or 1), zip(results.get('dram.total-queueing-delay', [0]*ncores), results['dram.accesses']))
    template.append(('  average dram queueing delay', 'dram.avgqueue', format_ns(2)))
  elif 'dram.total-backpressure-latency' in results:
    results['dram.avgbp'] = map(lambda (a,b): a/(b or 1), zip(results.get('dram.total-backpressure-latency', [0]*ncores), results['dram.accesses']))
    template.append(('  average dram backpressure delay', 'dram.avgbp', format_ns(2)))
  if 'dram-queue.total-time-used' in results:
    results['dram.bandwidth_util'] = map(lambda a: float(100)*a/time0 if time0 else float('inf'), results['dram-queue.total-time-used'])
    template.append(('  average dram queue utilization', 'dram.bandwidth_util', lambda v: '%.2f%%' % v))
  results['dram.bandwidth'] = map(lambda a: float(64*a)/(time0/1e6) if time0 else float('inf'), results['dram.accesses'])
  template.append(('  average dram bandwidth (GB/s)', 'dram.bandwidth', lambda v: '%.2f' % v))
  
  
  # CXL access Summary
  if 'cxl.data-reads' in results:
      template += [('CXL effective Access', '', '')]
      results['cxl.effective-read-latency'] = map(lambda (a,b): a/(b or 1), zip(results['cxl.total-effective-read-latency'], results['cxl.data-reads']))
      results['cxl.mac-accesses'] = map(sum, zip(results['cxl.mac-reads'], results['cxl.mac-writes']))
      results['cxl.data-accesses'] = map(sum, zip(results['cxl.data-reads'], results['cxl.data-writes']))
      results['cxl.data-bandwidth'] = map(lambda a: float(64*a)/(time0/1e6) if time0 else float('inf'), results['cxl.data-accesses'])
      template.append(('  num data reads', 'cxl.data-reads', str))
      template.append(('  avg read latency', 'cxl.effective-read-latency', format_ns(2)))
      template.append(('  num data writes', 'cxl.data-writes', str))
      template.append(('  num mac accesses', 'cxl.mac-accesses', str))
      template.append(('  data bandwidth (GB/s)', 'cxl.data-bandwidth', lambda v: '%.2f' % v))
  
  
  if 'cxl.reads' in results:
    results['cxl.accesses'] = map(sum, zip(results['cxl.reads'], results['cxl.writes']))
    results['cxl.avglatency'] = map(lambda (a,b): a/b if b else float('inf'), zip(results['cxl.total-access-latency'], results['cxl.accesses']))
    template += [
      ('  CXL summary', '', ''),
      ('    num cxl accesses', 'cxl.accesses', str),
      ('    average cxl access latency (ns)', 'cxl.avglatency', format_ns(2)),
    ]
    if 'cxl.total-read-queueing-delay' in results:
      results['cxl.avgqueueread'] = map(lambda (a,b): a/(b or 1), zip(results['cxl.total-read-queueing-delay'], results['cxl.reads']))
      results['cxl.avgqueuewrite'] = map(lambda (a,b): a/(b or 1), zip(results['cxl.total-write-queueing-delay'], results['cxl.writes']))
      template.append(('    average cxl read queueing delay', 'cxl.avgqueueread', format_ns(2)))
      template.append(('    average cxl write queueing delay', 'cxl.avgqueuewrite', format_ns(2)))
    else:
      results['cxl.avgqueue'] = map(lambda (a,b): a/(b or 1), zip(results.get('cxl.total-queueing-delay', [0]*ncores), results['cxl.accesses']))
      template.append(('    average cxl queueing delay', 'cxl.avgqueue', format_ns(2)))
    if 'cxl-queue.total-time-used' in results:
      results['cxl.bandwidth_util'] = map(lambda a: 100*a/time0 if time0 else float('inf'), results['cxl-queue.total-time-used'])
      template.append(('    average cxl bandwidth utilization', 'cxl.bandwidth_util', lambda v: '%.2f%%' % v))
    results['cxl.bandwidth'] = map(lambda a: float(64*a)/(time0/1e6) if time0 else float('inf'), results['cxl.accesses'])
    template.append(('    average cxl bandwidth (GB/s)', 'cxl.bandwidth', lambda v: '%.2f' % v))
    
  
    # Dram perfomance summary on CXL expander
    if 'vv.dram-reads' in results:
      results['vv.dram-accesses'] = map(sum, zip(results['vv.dram-reads'], results['vv.dram-writes']))
      results['cxl-dram.accesses'] = map(lambda (a,b): a if a>0 else b, zip(results['vv.dram-accesses'], results['cxl.accesses']))
    else:
      results['cxl-dram.accesses'] = results['cxl.accesses']
    results['cxl-dram.avglatency'] = map(lambda (a,b): a/b if b else float('inf'), zip(results['cxl-dram.total-access-latency'], results['cxl-dram.accesses']))
    template += [
      ('  CXL DRAM summary', '', ''),
      ('    num cxl dram accesses', 'cxl-dram.accesses', str),
      ('    average dram access latency(CXL expander) (ns)', 'cxl-dram.avglatency', format_ns(2)),
    ]
    if 'cxl-dram.total-queueing-delay' in results:
      results['cxl-dram.avgqueue'] = map(lambda (a,b): a/(b or 1), zip(results['cxl-dram.total-queueing-delay'], results['cxl-dram.accesses']))
      template.append(('    average cxl dram queueing delay', 'cxl-dram.avgqueue', format_ns(2)))
    elif 'cxl-dram.total-backpressure-latency' in results:
      results['cxl-dram.avgbp'] = map(lambda (a,b): a/(b or 1), zip(results.get('cxl-dram.total-backpressure-latency', [0]*ncores), results['cxl-dram.accesses']))
      template.append(('    average cxl dram backpressure delay', 'cxl-dram.avgbp', format_ns(2)))
    if 'cxl-dram-queue.total-time-used' in results:
      results['cxl-dram.bandwidth_util'] = map(lambda a: 100*a/time0 if time0 else float('inf'), results['cxl-dram-queue.total-time-used'])
      template.append(('    average cxl dram bandwidth utilization', 'cxl-dram.bandwidth_util', lambda v: '%.2f%%' % v))
    results['cxl-dram.bandwidth'] = map(lambda a: float(64*a)/(time0/1e6) if time0 else float('inf'), results['cxl-dram.accesses'])
    template.append(('    average cxl dram bandwidth (GB/s)', 'cxl-dram.bandwidth', lambda v: '%.2f' % v))
    
  
  if 'vv.total-read-delay' in results:
    results['vv.reads'] = map(lambda(a,b): a if b!=0 else float(0), zip(results['cxl.reads'], results['vv.dram-reads']))
    results['vv.updates'] = map(lambda(a,b): a if b!=0 else float(0), zip(results['cxl.writes'], results['vv.dram-reads']))
    results['vv.avg-read-latency'] = map(lambda (a,b): a/b if b else float('inf'), zip(results['vv.total-read-delay'], results['cxl.reads']))
    results['vv.avg-update-latency'] = map(lambda (a,b): a/b if b else float('inf'), zip(results['vv.total-update-latency'], results['cxl.writes']))
    template += [
      ('VN Vault summary', '', ''),
      ('  num vn-vault reads', 'vv.reads', str),
      ('  average vn-vault read latency (ns)', 'vv.avg-read-latency', format_ns(2)),
      ('  num vn-vault updates', 'vv.updates', str),
      ('  average vn-vault update latency (ns)', 'vv.avg-update-latency', format_ns(2)),
    ]
    
  # Encrypt and Gen MAC: one AES access + one MAC access. 
  # Decrypt and Verify: one AES access
  # fetchMACVN: one MAC access + one VN acceses
  
  if 'mee.enc-mac' in results:
    results['mee.aes'] = map(sum, zip(results['mee.enc-mac'], results['mee.dec-verify']))
    results['mee.mac'] = map(sum, zip(results['mee.vnmac-fetch'], results['mee.enc-mac']))
    results['mee.vn'] = results['mee.vnmac-fetch']
    
    template += [
      ('MEE summary', '', ''),
      ('  num mee crypto', 'mee.aes', str),
    ]
    if 'mee.total-aes-latency' in results:
      results['mee.avglatency'] = map(lambda (a,b): a/b if b else float('inf'), zip(results['mee.total-aes-latency'], results['mee.aes']))
      results['mee.avgqueue'] = map(lambda (a,b): a/b if b else float('inf'), zip(results['mee.total-queueing-delay'], results['mee.aes']))
      template.append(('  average mee crypto latency (ns)', 'mee.avglatency', format_ns(2)))
      template.append(('  average mee crypto queue latency (ns)', 'mee.avgqueue', format_ns(2)))
    if 'mee-queue.total-time-used' in results:
      results['mee.bandwidth'] = map(lambda a: 100*a/time0 if time0 else float('inf'), results['mee-queue.total-time-used'])
      template.append(('  average mee bandwidth utilization', 'mee.bandwidth', lambda v: '%.2f%%' % v))
      
    template += [ ('  MEE MAC Cache', '',''), 
                  ('    num mac access', 'mee.mac', str)]
    if 'mee.mac-misses' in results:
      results['mee.mac-missrate'] = map(lambda (a,b): float(100)*a/float(b) if b else float('inf'), zip(results['mee.mac-misses'], results['mee.mac']))
      template.append(('    num mac misses', 'mee.mac-misses', str))
      template.append(('    mac cache miss rate', 'mee.mac-missrate', lambda v: '%.2f%%' % v))
      
    template += [ ('  MEE VN Table', '',''), 
                  ('    num vn access', 'mee.vn', str)]
    if 'mee.vn-misses' in results:
      results['mee.vn-missrate'] = map(lambda (a,b): float(100)*a/float(b) if b else float('inf'), zip(results['mee.vn-misses'], results['mee.vn']))
      template.append(('    num vn misses', 'mee.vn-misses', str))
      template.append(('    vn table miss rate', 'mee.vn-missrate', lambda v: '%.2f%%' % v))
  
  if 'L1-D.loads-where-dram-local' in results:
    results['L1-D.loads-where-dram'] = map(sum, zip(results['L1-D.loads-where-dram-local'], results['L1-D.loads-where-dram-remote']))
    results['L1-D.stores-where-dram'] = map(sum, zip(results['L1-D.stores-where-dram-local'], results['L1-D.stores-where-dram-remote']))
    template.extend([
        ('Coherency Traffic', '', ''),
        ('  num loads from dram', 'L1-D.loads-where-dram' , str),
        #('  num stores from dram', 'L1-D.stores-where-dram' , str),
        ('  num loads from dram cache', 'L1-D.loads-where-dram-cache' , str),
        #('  num stores from dram cache', 'L1-D.stores-where-dram-cache' , str),
        ('  num loads from remote cache', 'L1-D.loads-where-cache-remote' , str),
        #('  num stores from remote cache', 'L1-D.stores-where-cache-remote' , str),
      ])

  lines = []
  lines.append([''] + [ 'Core %u' % i for i in range(ncores) ])

  for title, name, func in template:
    line = [ title ]
    if name and name in results:
      for core in range(ncores):
        line.append(' '+func(results[name][core]))
    else:
      line += [''] * ncores
    lines.append(line)


  widths = [ max(10, max([ len(l[i]) for l in lines ])) for i in range(len(lines[0])) ]
  for j, line in enumerate(lines):
    output.write(' | '.join([ ('%%%s%us' % ((j==0 or i==0) and '-' or '', widths[i])) % line[i] for i in range(len(line)) ]) + '\n')



if __name__ == '__main__':
  def usage():
    print 'Usage:', sys.argv[0], '[-h (help)] [--partial <section-start>:<section-end> (default: roi-begin:roi-end)] [-d <resultsdir (default: .)>]'

  jobid = 0
  resultsdir = '.'
  partial = None

  try:
    opts, args = getopt.getopt(sys.argv[1:], "hj:d:", [ 'partial=' ])
  except getopt.GetoptError, e:
    print e
    usage()
    sys.exit()
  for o, a in opts:
    if o == '-h':
      usage()
      sys.exit()
    if o == '-d':
      resultsdir = a
    if o == '-j':
      jobid = long(a)
    if o == '--partial':
      if ':' not in a:
        sys.stderr.write('--partial=<from>:<to>\n')
        usage()
      partial = a.split(':')

  if args:
    usage()
    sys.exit(-1)

  generate_simout(jobid = jobid, resultsdir = resultsdir, partial = partial)
