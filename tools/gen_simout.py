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

  results['dram.accesses'] = map(sum, zip(results['dram.reads'], results['dram.writes']))
  results['dram.avglatency'] = map(lambda (a,b): a/b if b else float('inf'), zip(results['dram.total-access-latency'], results['dram.accesses']))
  template += [
    ('DRAM summary', '', ''),
    ('  num dram accesses', 'dram.accesses', str),
    ('  num dram writes', 'dram.writes', str),
    ('  average dram access latency (ns)', 'dram.avglatency', format_ns(2)),
  ]
  if 'dram.total-read-latency' in results:
    results['dram.avgreadlat'] = map(lambda (a,b): a/b if b else float('inf'), zip(results['dram.total-read-latency'], results['dram.reads']))
    template.append(('  average dram read latency (ns)', 'dram.avgreadlat', format_ns(2)))
  if 'dram.data-reads' in results:
    results['dram.data-accesses'] = map(sum, zip(results['dram.data-reads'], results['dram.data-writes']))
    results['dram.mac-accesses'] = map(sum, zip(results['dram.mac-reads'], results['dram.mac-writes']))
    template.append(('  num data accesses', 'dram.data-accesses', str))
    template.append(('  num mac accesses', 'dram.mac-accesses', str))
  if 'dram.total-read-queueing-delay' in results:
    results['dram.avgqueueread'] = map(lambda (a,b): a/(b or 1), zip(results['dram.total-read-queueing-delay'], results['dram.reads']))
    results['dram.avgqueuewrite'] = map(lambda (a,b): a/(b or 1), zip(results['dram.total-write-queueing-delay'], results['dram.writes']))
    template.append(('  average dram read queueing delay', 'dram.avgqueueread', format_ns(2)))
    template.append(('  average dram write queueing delay', 'dram.avgqueuewrite', format_ns(2)))
  elif 'dram.avgqueue' in results:
    results['dram.avgqueue'] = map(lambda (a,b): a/(b or 1), zip(results.get('dram.total-queueing-delay', [0]*ncores), results['dram.accesses']))
    template.append(('  average dram queueing delay', 'dram.avgqueue', format_ns(2)))
  if 'dram-queue.total-time-used' in results:
    results['dram.bandwidth'] = map(lambda a: float(100)*a/time0 if time0 else float('inf'), results['dram-queue.total-time-used'])
    template.append(('  average dram bandwidth utilization', 'dram.bandwidth', lambda v: '%.2f%%' % v))
  results['dram.bandwidth'] = map(lambda a: float(64*a)/(time0/1e6) if time0 else float('inf'), results['dram.accesses'])
  template.append(('  average dram bandwidth (GB/s)', 'dram.bandwidth', lambda v: '%.2f' % v))
  # CXL access Summary
  if 'cxl.reads' in results:
    results['cxl.accesses'] = map(sum, zip(results['cxl.reads'], results['cxl.writes']))
    results['cxl.avglatency'] = map(lambda (a,b): a/b if b else float('inf'), zip(results['cxl.total-access-latency'], results['cxl.accesses']))
    template += [
      ('CXL summary', '', ''),
      ('  num cxl accesses', 'cxl.accesses', str),
      ('  average cxl access latency (ns)', 'cxl.avglatency', format_ns(2)),
    ]
    if 'cxl.data-reads' in results:
      results['cxl.data-accesses'] = map(sum, zip(results['cxl.data-reads'], results['cxl.data-writes']))
      results['cxl.mac-accesses'] = map(sum, zip(results['cxl.mac-reads'], results['cxl.mac-writes']))
      template.append(('  num data accesses', 'cxl.data-accesses', str))
      template.append(('  num mac accesses', 'cxl.mac-accesses', str))
    if 'cxl.total-read-queueing-delay' in results:
      results['cxl.avgqueueread'] = map(lambda (a,b): a/(b or 1), zip(results['cxl.total-read-queueing-delay'], results['cxl.reads']))
      results['cxl.avgqueuewrite'] = map(lambda (a,b): a/(b or 1), zip(results['cxl.total-write-queueing-delay'], results['cxl.writes']))
      template.append(('  average cxl read queueing delay', 'cxl.avgqueueread', format_ns(2)))
      template.append(('  average cxl write queueing delay', 'cxl.avgqueuewrite', format_ns(2)))
    else:
      results['cxl.avgqueue'] = map(lambda (a,b): a/(b or 1), zip(results.get('cxl.total-queueing-delay', [0]*ncores), results['cxl.accesses']))
      template.append(('  average cxl queueing delay', 'cxl.avgqueue', format_ns(2)))
    if 'cxl-queue.total-time-used' in results:
      results['cxl.bandwidth'] = map(lambda a: 100*a/time0 if time0 else float('inf'), results['cxl-queue.total-time-used'])
      template.append(('  average cxl bandwidth utilization', 'cxl.bandwidth', lambda v: '%.2f%%' % v))
  
  if 'vn-vault.reads' in results:
    results['vn-vault.accesses'] = map(sum, zip(results['vn-vault.reads'], results['vn-vault.updates']))
    results['vn-vault.avglatency'] = map(lambda (a,b): a/b if b else float('inf'), zip(results['vn-vault.total-access-latency'], results['vn-vault.accesses']))
    template += [
      ('VN Vault summary', '', ''),
      ('  num vn-vault accesses', 'vn-vault.accesses', str),
      ('  average vn-vault access latency (ns)', 'vn-vault.avglatency', format_ns(2)),
    ]
    if 'vn-vault.total-queueing-delay' in results:
      results['vn-vault.avgqueue'] = map(lambda (a,b): a/(b or 1), zip(results['vn-vault.total-queueing-delay'], results['vn-vault.accesses']))
      template.append(('  average vn-vault queueing delay', 'vn-vault.avgqueue', format_ns(2)))
    if 'vn-queue.total-time-used' in results:
      results['vn-vault.bandwidth'] = map(lambda a: 100*a/time0 if time0 else float('inf'), results['vn-queue.total-time-used'])
      template.append(('  average cxl bandwidth utilization', 'vn-vault.bandwidth', lambda v: '%.2f%%' % v))
    
  
  if 'page_table.pages' in results:
    results['page_table.usage'] = map(lambda a: float(config['system/addr_trans/page_size']) * a / (1024*1024), results['page_table.pages'])
    template += [
      ('Page Tabel summary', '', ''),
      ('  num pages [cxl, dram]', 'page_table.pages', str),
      ('  usage (GB) [cxl, dram]', 'page_table.usage', lambda v: '%.4f' % v),
    ]
  
  if 'mee.encrypts' in results:
    results['mee.crypto'] = map(sum, zip(results['mee.encrypts'], results['mee.decrypts']))
    results['mee.mac'] = map(sum, zip(results['mee.mac-gen'], results['mee.mac-verify']))
    results['mee.accesses'] = map(sum, zip(results['mee.crypto'], results['mee.mac']))
    results['mee.crypto-avglatency'] = map(lambda (a,b): a/b if b else float('inf'), zip(results['mee.total-crypto-latency'], results['mee.crypto']))
    results['mee.mac-avglatency'] = map(lambda (a,b): a/b if b else float('inf'), zip(results['mee.total-mac-latency'], results['mee.mac']))
    template += [
      ('MEE summary', '', ''),
      ('  num mee crypto', 'mee.crypto', str),
      ('  num mee mac gen', 'mee.mac', str),
      ('  average mee crypto latency (ns)', 'mee.crypto-avglatency', format_ns(2)),
      ('  average mac crypto latency (ns)', 'mee.mac-avglatency', format_ns(2)),
    ]
    if 'mee.total-encrypt-queueing-delay' in results:
      results['mee.avgqueueencrypt'] = map(lambda (a,b): a/(b or 1), zip(results['mee.total-encrypt-queueing-delay'], results['mee.encrypts']))
      results['mee.avgqueuedecrypt'] = map(lambda (a,b): a/(b or 1), zip(results['mee.total-decrypt-queueing-delay'], results['mee.decrypts']))
      results['mee.avgqueuemac'] = map(lambda (a,b): a/(b or 1), zip(results['mee.total-mac-queueing-delay'], results['mee.macs']))
      template.append(('  average mee encrypt queueing delay', 'mee.avgqueueencrypt', format_ns(2)))
      template.append(('  average mee decrypt queueing delay', 'mee.avgqueuedecrypt', format_ns(2)))
      template.append(('  average mee mac queueing delay', 'mee.avgqueuemac', format_ns(2)))
    else:
      results['mee.avgqueue'] = map(lambda (a,b): a/(b or 1), zip(results.get('mee.total-queueing-delay', [0]*ncores), results['mee.accesses']))
      template.append(('  average mee queueing delay', 'mee.avgqueue', format_ns(2)))
    if 'mee-queue.total-time-used' in results:
      results['mee.bandwidth'] = map(lambda a: 100*a/time0 if time0 else float('inf'), results['mee-queue.total-time-used'])
      template.append(('  average mee bandwidth utilization', 'mee.bandwidth', lambda v: '%.2f%%' % v))
    
    if 'mee.mac_gen_misses' in results:
      results['mee.mac_accesses'] = map(sum, zip(results['mee.mac-gen'], results['mee.mac-fetch']))
      results['mee.mac_misses'] = map(sum, zip(results['mee.mac-gen-misses'], results['mee.mac-fetch-misses']))
      results['mee.mac_missrate'] = map(lambda (a,b): float(100)*a/float(b) if b else float('inf'), zip(results['mee.mac_misses'], results['mee.mac_accesses']))
      template.append(('  num mac cache accesses', 'mee.mac_accesses', str))
      template.append(('  num mac cache misses', 'mee.mac_misses', str))
      template.append(('  mac cache miss rate', 'mee.mac_missrate', lambda v: '%.2f%%' % v))
  
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
