TARGET=fft
include ../shared/Makefile.shared

$(TARGET): $(TARGET).o
	$(CC) $(TARGET).o -lm $(SNIPER_LDFLAGS) -o $(TARGET)

run_$(TARGET):
	../../run-sniper -v -n 1 -c icelake --roi -- ./fft -p 1 -g perf_model/l2_cache/shared_cores=2
	../../tools/gen_topology.py 

CLEAN_EXTRA=viz
