all: ddr_spd_reader ddr_rcd_reader

ddr_spd_reader: ddr_spd_reader.c
	gcc `pkg-config --cflags libftdi` -o ddr_spd_reader  ddr_spd_reader.c  `pkg-config --libs libftdi`

ddr_rcd_reader: ddr_rcd_reader.c
	gcc `pkg-config --cflags libftdi` -o ddr_rcd_reader  ddr_rcd_reader.c  `pkg-config --libs libftdi`

clean:
	rm ddr_spd_reader ddr_rcd_reader
