# SniperSim Simulator for Toleo 
This is a sniper sim based simulator for Toleo (ASPLOS'25) Please refer to our paper "Toleo: Scaling Freshness to Tera-scale Memory
Using CXL and PIM" for details. 

# Getting Started
Running evaluation for Toleo requires: 
1. Install DRAMSim3 from this [fork](https://github.com/joydddd/DRAMsim3). 
2. Install simulator sniper-toleo. (this repo) 
3. setup benchmarks with PIN hooks. Forks with PIN hooks can be found in this [list](https://github.com/stars/joydddd/lists/toleo)
4. Download simulation [script](https://raw.githubusercontent.com/joydddd/VNserver_spec/main/run_toleo_sim.py) `run_toleo_sim.py` and start simulation
5. Find simulation results in the benchmark destination folder.


## File Structure
```
toleo
├── run_toleo_sim.py
├── DRAMsim3
├── sniper-toleo
│   ├── DRAMsim3 -> ../DRAMsim3 (link)
│   ├── run-sniper
│   └── README.md (this file)
├── genomicsbench
│   └── benchmarks
│      ├── fmi
│      │  └── sim-<date-time>/<region>/sim.out
│      ├── bsw-s
│      ...
├── gabps
│   └── run
│      ├── pr-kron-s
│      │  └── sim-<date-time>/<region>/sim.out
│      ├── sssp
│      ....
├── redis
├── memtier_benchmark
├── memcached
├── hyrise
└── llama2.c
```

## Make TOLEO_ROOT directory
```
mkdir toleo
cd toleo
```

## Install DRAMsim3
```
git clone https://github.com/joydddd/DRAMsim3
cd DRAMsim3
```
Please follow the README from this [fork](https://github.com/joydddd/DRAMsim3) to build DRAMsim3. THERMAL is not required for toleo simulation. 
Run DRAMsim3 test to make sure it is sucessfully installed:
```
 ./build/dramsim3main configs/DDR4_8Gb_x8_3200.ini -c 100000 -t tests/example.trace
cat dramsim3.json
```

## Install SniperSim for Toleo
### Prerequisite
- A DRAMsim3 installation from this [fork](https://github.com/joydddd/DRAMsim3), and link to sniper-toleo repo.
```
## in folder toleo. 
git clone git@github.com:joydddd/sniper-toleo.git # clone sniper-toleo (this repo)
cd sniper-toleo
ln -s ../DRAMsim3 DRAMsim3
```

### Install SniperSim
Please follow the naive install instructions on Sniper Sim [Getting Started Page](https://snipersim.org/w/Getting_Started) to install sniper-toleo. Necessary steps are provided below

1. Install dependent packages.
```
sudo dpkg --add-architecture i386
sudo apt-get install binutils build-essential curl git libboost-dev libbz2-dev libc6:i386 libncurses5:i386 libsqlite3-dev libstdc++6:i386 python wget zlib1g-dev
```

2. build simulator with PIN
```
make USE_PIN=1 -j N #where N is the number of cores in your machine to use parallel make
```
> [!NOTE]
> Know issue: snipersim assum python2 as the default python version. Make sure your python command points to python2.7.

3. Test run
```
cd test/fft
make run
```

## Setup Genomoicsbench and Gapbs Benchmark Set
Here we provide detailed instructions for setting up genomicsbench and gapbs benchmarks. 


Confirm sample benchmarks are set up correctly. 
```
toleo_sim.py native --bench bsw-s --arch zen4_cxl
```

## Running Sample Testcases 
Use the `run_toleo_sim.py` script to start a simulation. 
Simulate a node with memory protected by Toleo. 
```
./run_toleo_sim.py sniper --bench bsw-s --arch zen4_vn -a
```
Simulate without memory protection
```
./run_toleo_sim.py sniper --bench bsw-s --arch zen4_cxl -a
```
Simulate memory with confidentiality and integrity protection. (CI) 
```
toleo_sim.py sniper --bench bsw-s --arch zen4_no_freshness -a
```
Simulate invisiMem
```
toleo_sim.py sniper --bench bsw-s --arch zen4_cxl_invisimem -a
```




(original README from sinpersim) 
-------------------------------------------------------------------------

This is the source code for the Sniper multicore simulator developed
by the Performance Lab research group at Ghent University, Belgium.
Please refer to the NOTICE file in the top level directory for
licensing and copyright information.

For the latest version of the software or additional information, please
see our website:

http://snipersim.org

If you are using Sniper, please let us know by posting a message on
our user forum.  If you use Sniper 6.0 or later in your research,
(if you are using the Instruction-Window Centric core model, etc.),
please acknowledge us by referencing our TACO 2014 paper:

Trevor E. Carlson, Wim Heirman, Stijn Eyerman, Ibrahim Hur, Lieven
Eeckhout, "An Evaluation of High-Level Mechanistic Core Models".
In ACM Transactions on Architecture and Code Optimization (TACO),
Volume 11, Issue 3, October 2014, Article No. 28
http://dx.doi.org/10.1145/2629677

If you are using earlier versions of Sniper, please acknowledge
us by referencing our SuperComputing 2011 paper:

Trevor E. Carlson, Wim Heirman, Lieven Eeckhout, "Sniper: Exploring
the Level of Abstraction for Scalable and Accurate Parallel Multi-Core
Simulation". Proceedings of the International Conference for High
Performance Computing, Networking, Storage and Analysis (SC),
pages 52:1--52:12, November 2011.
http://dx.doi.org/10.1145/2063384.2063454
