# Benchmark Setup Instructions
Find benchmarks in this [list](https://github.com/stars/joydddd/lists/toleo). 
- [Genomebench](https://github.com/joydddd/Genomebench)
  - bsw
  - fmi
  - chain
  - dbg
  - pileup
- [Gapbs](https://github.com/joydddd/gapbs)
  - bfs
  - pr
  - sssp
- [llama2.c](https://github.com/joydddd/llama2.c)
- [memcached](https://github.com/joydddd/memcached)
- [redis](https://github.com/joydddd/redis)
  - [memtier_benchmark](https://github.com/joydddd/memtier_benchmark) (workload generator for memcached & redis) 
- [hyrise](https://github.com/joydddd/hyrise) (comes with built-in workload generator tpcc) 
## GenomicsBench
1. clone [GenomicsBench](https://github.com/joydddd/Genomebench)
```
# PWD = toleo_root
git clone --recursive git@github.com:joydddd/Genomebench.git genomicsbench
cd genomicsbench
```
2. follow instructions in [GenomicsBench](https://github.com/joydddd/Genomebench) to download the dataset, install dependencies and compile for cpu benchmark. 

Essential commands:
```
wget https://genomicsbench.eecs.umich.edu/input-datasets.tar.gz
tar -zxf input-datasets.tar.gz
sudo apt-get install $(cat debian.prerequisites)
make -j12
```
## Gaps
TODO

## llama2.c

TODO

## key-value store: memcached & redis

TODO

## hyrise

TODO
