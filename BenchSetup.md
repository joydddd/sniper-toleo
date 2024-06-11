# Benchmark Setup Instructions
Find benchmarks in this [list](https://github.com/stars/joydddd/lists/toleo). 
- [Genomebench](https://github.com/joydddd/Genomebench)
  - (sample test) bsw-s
  - bsw
  - fmi
  - chain
  - dbg-s
  - pileup-s
- [Gapbs](https://github.com/joydddd/gapbs)
  - (sample test) pr-kron-s
  - bfs-twitter
  - pr-kron
  - sssp-road
- [llama2.c](https://github.com/joydddd/llama2.c)
  - llama-5
  - llama-8 (Toleo space analysis only)
- [memcached](https://github.com/joydddd/memcached)
  - memcached-test
- [redis](https://github.com/joydddd/redis)
  - redis-5kw
  - [memtier_benchmark](https://github.com/joydddd/memtier_benchmark) (workload generator for memcached & redis) 
- [hyrise](https://github.com/joydddd/hyrise) (comes with built-in workload generator tpcc)
  - hyrise
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
3. Test run
```
# in toleo_root
./run_toleo_sim.py native --bench genomicsbench
```
Results can be found in `toleo_root/genomicsbench/benchmarks/<bench-name>/`
## Gapbs
1. clone [Gapbs](https://github.com/joydddd/gapbs) and build kernels. 
```
make -j 14 # build kernels
```
2. Generate/download datasets. 
```
make benchmark/graphs/twitter.sg
make benchmark/graphs/kron-g27.sg
make benchmark/graphs/kron-g22.sg
make benchmark/graphs/road.wsg
```
3. Create a run folder for each test case
```
mkdir run
mkdir run/pr-kron-s
mkdir run/pr-kron
mkdir run/sssp-road
mkdir run/bfs-twitter
```
4. Test run
   
```
# in toleo_root
./run_toleo_sim.py native --bench graphbench
```
Results can be found in `toleo_root/gapbs/run/<bench-name>/`
## llama2.c
1. clone and build [llama2.c](https://github.com/joydddd/llama2.c)
2. Download llama2-7b model [instructions](https://github.com/joydddd/llama2.c?tab=readme-ov-file#metas-llama-2-models)
   Put the model in `model` folder and create the simulation folder.
```
mkdir sim
mkdir sim/llama-5
mkdir sim/llama-8
```
```
└── llama2.c
    ├── run
    ├── sim
    │    ├── llama-8
    │    └── llama-5
    └── model
        ├── llama2_7b.bin
        └── llama-2-7b
```
     
3. Test run
 ```
# in toleo_root
./run_toleo_sim.py native --bench llama2bench
```
Results can be found in `toleo_root/llama2.c/sim/<llama-8/llama-5>/`.
## key-value store: memcached & redis

TODO

## hyrise

1. clone hyrise from this [fork](https://github.com/joydddd/hyrise)
```
git clone git@github.com:joydddd/hyrise.git
cd hyrise
git submodule update --init --recursive
```
2. build hyrise
```
mkdir build
cd build
cmake -DHYRISE_RELAXED_BUILD=On ..
CPLUS_INCLUDE_PATH=<path_to_toleo_root>:$CPLUE_INCLUDE_PATH/sniper-toleo/include make -j <number of threads> # build with sniper-toleo/include in include path
```
3. build run directory
```
#in hyrise root
mkdir -p run/hyrise
```
```
hyrise
  ├── build
  ├── run
  │    └── hyrise
  │        └── tpcc_cache_tables/sf-10
  └── build
      └── hyriseBnechmarkTPCC
          
```
4. Native run
```
# in toleo_root
./run_toleo_sim.py native --bench hyrise
```
> [!WARNING]
>  The first run of hyrise builds a cache for tpcc tables (`run/hyrise/tpcc_cache_tables/sf-10`). This may take a while (several minutes). Please make sure this cache is properly built before running sniper simulation for hyrise.  

> [!NOTE]
> Hyrise database takes a relatively long time to warmup (minutes in native mode, hours in simulation mode).
