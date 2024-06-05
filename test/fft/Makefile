TARGET=fft
include ../shared/Makefile.shared

$(TARGET): $(TARGET).o
	$(CC) $(TARGET).o -lm $(SNIPER_LDFLAGS) -o $(TARGET)

zen4_vn:
	mkdir -p toleo_output
	../../run-sniper -v -n 32 -c zen4_vn --roi -d toleo_output  -- ./fft -p 1

zen4_cxl:
	mkdir -p no_protection_output
	../../run-sniper -v -n 32 -c zen4_vn --roi -d no_protection_output  -- ./fft -p 1

zen4_no_freshness:
	mkdir -p ci_output
	../../run-sniper -v -n 32 -c zen4_vn --roi -d ci_output  -- ./fft -p 1

run_$(TARGET):zen4_vn zen4_cxl zen4_no_freshness

CLEAN_EXTRA=viz *.trace *.txt *.json dramsim_**