TARGET=fft
include ../shared/Makefile.shared

$(TARGET): $(TARGET).o
	$(CC) $(TARGET).o -lm $(SNIPER_LDFLAGS) -o $(TARGET)

run_$(TARGET):
	../../run-sniper -v -n 32 -c zen4_s --roi  -- ./fft -p 1
	../../tools/gen_topology.py 

CLEAN_EXTRA=viz *.trace *.txt *.json dramsim_**