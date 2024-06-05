# SniperSim Simulator for Toleo 
This is a sniper sim based simulator for Toleo (ASPLOS'25) Please refer to our paper "Toleo: Scaling Freshness to Tera-scale Memory
Using CXL and PIM" for details. 

# Getting Started
Running evaluation for Toleo requires: 
1. Install DRAMSim3 from this [fork](https://github.com/joydddd/DRAMsim3). 
2. Install simulator sniper-toleo. (this repo) 
3. setup benchmarks with PIN hooks. Forks with PIN hooks can be found in this [list](https://github.com/stars/joydddd/lists/toleo)
4. Download simulation [script](https://raw.githubusercontent.com/joydddd/VNserver_spec/main/run_toleo_sim.py) `run_toleo_sim.py` and start simulation


## File Structure
```
toleo_root
├── run_toleo_sim.py
├── DRAMsim3
├── sniper-toleo
│   ├── DRAMsim3 -> ../DRAMsim3 (link)
│   ├── run-sniper
│   └── README.md (this file)
├── genomicsbench
│   ├── input-datasets
│   │    ├── bsw
│   │    ├── chain
│   │    ...
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

Make TOLEO_ROOT directory
```
mkdir toleo_root
cd toleo_root
```

## Install DRAMsim3
```
git clone https://github.com/joydddd/DRAMsim3
cd DRAMsim3
```
Please follow the README from this [fork](https://github.com/joydddd/DRAMsim3) to build DRAMsim3. THERMAL is not required for toleo simulation. 
Run DRAMsim3 test to make sure it is successfully installed:
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

### Test run
```
cd test/fft
make
```
This command runs sniper-toleo simulation for three setups: Toleo, C+I, and no-protections. Simulation results can be found in folders `test/fft/no_protection_output` `test/fft/ci_output` `test/fft/toleo_output` respectively. 


## Setup Benchmark 
Simulating benchmarks on sniper-toleo requires a special PIN hook manually inserted into the source code. In this [list](https://github.com/stars/joydddd/lists/toleo) we provide forks of the benchmark sets with the PIN_HOOK inserted. 
### Sample Benchmark Suite setup: GenomicsBench
Here we provide detailed instructions for setting up genomicsbench. Please follow this [fork](https://github.com/joydddd/Genomebench) to clone the source code for Genomics Bench. 
```
# PWD = toleo_root
git clone --recursive git@github.com:joydddd/Genomebench.git genomicsbench
cd genomicsbench
```

follow instructions in [GenomicsBench](https://github.com/joydddd/Genomebench) to download the dataset, install dependencies and compile for cpu benchmark. 
```
wget https://genomicsbench.eecs.umich.edu/input-datasets.tar.gz
tar -zxf input-datasets.tar.gz
sudo apt-get install $(cat debian.prerequisites)
make -j12
```

## Run Toleo Simulation on Benchmarks
Download simulation [script](https://raw.githubusercontent.com/joydddd/VNserver_spec/main/run_toleo_sim.py) `run_toleo_sim.py` 
```
# in toleo_root
wget https://github.com/joydddd/VNserver_spec/raw/main/run_toleo_sim.py
chmod +x run_toleo_sim.py
./run_toleo_sim.py --help
```

### Sample Benchmark Suite Setup: Gapbs

TODO: Add setup ins for gapbs

### Native Run
First, we want to ensure the benchmark runs natively with our script and reports a kernel runtime + peak RSS ussage. 

```
./run_toleo_sim.py native --bench genomicsbench # this instruction runs all tests in genomics benchmark
./run_toleo_sim.py native --bench bsw-s # run bsw-s benchmark
```
The program will print `PIN_START` and `PIN_END` in the terminal when encountering its PIN hooks and entering the region of interest (ROI). 

### start simulation 

Use the `run_toleo_sim.py` script to start a simulation. This may take a long time, so we suggest starting with the `sim_test` suite ('bsw-s` from genomicsbench, and `pr-kron-s` from gapbs. Both should simulate in less than 20 minutes).

Simulate Toleo
```
./run_toleo_sim.py sniper --bench bsw-s --arch zen4_vn -a
```

Simulate no memory protection
```
./run_toleo_sim.py sniper --bench bsw-s --arch zen4_cxl -a
```

Simulate memory with confidentiality and integrity protection and no freshness protection. (CI) 
```
toleo_sim.py sniper --bench bsw-s --arch zen4_no_freshness -a
```

> [!WARNING]
> This requires using sniper-toleo from branch `vnserver`.


Simulate invisiMem baseline
```
toleo_sim.py sniper --bench bsw-s --arch zen4_cxl_invisimem -a
```
