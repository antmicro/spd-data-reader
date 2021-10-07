all: ddr_spd_reader

ddr_spd_reader: ddr_spd_reader.c
	gcc `pkg-config --cflags libftdi` -o ddr_spd_reader  ddr_spd_reader.c  `pkg-config --libs libftdi`
