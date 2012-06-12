#!/usr/bin/env python

import sys, sniper_lib


def generate_simout(jobid = None, resultsdir = None, output = sys.stdout, silent = False):

  try:
    results = sniper_lib.get_results(jobid = jobid, resultsdir = resultsdir)['results']
  except (KeyError, ValueError), e:
    if not silent:
      print 'Failed to generated sim.out:', e
    return


  format_int = lambda v: str(long(v))
  def format_ns(digits):
    return lambda v: ('%%.%uf' % digits) % (v/1e6)

  template = [
    ('  Instructions', 'performance_model.instruction_count', str),
    ('  Cycles',       'performance_model.cycle_count',       format_int),
    ('  Time',         'performance_model.elapsed_time',      format_ns(0)),
    ('Branch predictor stats', '', ''),
    ('  num correct',  'branch_predictor.num-correct', str),
    ('  num incorrect','branch_predictor.num-incorrect', str),
    ('Cache Summary', '', ''),
  ]

  for c in [ 'L1-I', 'L1-D' ] + [ 'L%u'%l for l in range(2, 5) if 'L%u.loads'%l in results ]:
    results['%s.accesses'%c] = map(sum, zip(results['%s.loads'%c], results['%s.stores'%c]))
    results['%s.misses'%c] = map(sum, zip(results['%s.load-misses'%c], results['%s.store-misses'%c]))
    results['%s.missrate'%c] = map(lambda (a,b): 100*a/float(b or 1), zip(results['%s.misses'%c], results['%s.accesses'%c]))
    template.extend([
      ('  Cache %s'%c, '', ''),
      ('    num cache accesses', '%s.accesses'%c, str),
      ('    miss rate', '%s.missrate'%c, lambda v: '%.2f%%' % v),
      ('    num cache misses', '%s.misses'%c, str),
    ])

  results['dram.accesses'] = map(sum, zip(results['dram.reads'], results['dram.writes']))
  results['dram.avglatency'] = map(lambda (a,b): a/(b or 1), zip(results['dram.total-access-latency'], results['dram.accesses']))
  results['dram.avgqueue'] = map(lambda (a,b): a/(b or 1), zip(results['dram.total-queueing-delay'], results['dram.accesses']))
  template += [
    ('DRAM summary', '', ''),
    ('  num dram accesses', 'dram.accesses', str),
    ('  average dram access latency', 'dram.avglatency', format_ns(2)),
    ('  average dram queueing delay', 'dram.avgqueue', format_ns(2)),
  ]


  lines = []
  lines.append([''] + [ 'Core %u' % i for i in range(results['ncores']) ])

  for title, name, func in template:
    line = [ title ]
    if name and name in results:
      for core in range(results['ncores']):
        line.append(' '+func(results[name][core]))
    else:
      line += [''] * results['ncores']
    lines.append(line)


  widths = [ max(10, max([ len(l[i]) for l in lines ])) for i in range(len(lines[0])) ]
  for j, line in enumerate(lines):
    output.write(' | '.join([ ('%%%s%us' % ((j==0 or i==0) and '-' or '', widths[i])) % line[i] for i in range(len(line)) ]) + '\n')



if __name__ == '__main__':
  if len(sys.argv) > 1:
    resultsdir = sys.argv[1]
  else:
    resultsdir = '.'
  generate_simout(resultsdir = resultsdir)