TARGET=fft
include ../shared/Makefile.shared

$(TARGET): $(TARGET).o
	$(CC) $(TARGET).o -lm $(SNIPER_LDFLAGS) -o $(TARGET)

run_$(TARGET):
	../../run-sniper -v -n 1 -c saphire_rapid_s --gdb --roi -- ./fft -p 1
	../../tools/gen_topology.py 

CLEAN_EXTRA=viz *.trace
