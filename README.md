SPD Data Reader
===============

Copyright (c) 2021 [Antmicro](https://www.antmicro.com)

Serial Presence Detect ([SPD](https://en.wikipedia.org/wiki/Serial_presence_detect)) is a standard way of accessing the information of a memory module.

SPD makes use of the I2C protocol, and this program uses the FTDI library to bridge
between the host and the memory module.

At the moment, SPD data can be read from the FTDI for each memory module type,
but the data decoding and display is only available for DDR4 memory types.

Usage
-----

Install dependencies:

```
sudo apt install libftdi-dev
```

Compile:
```
make
```

Run:
```
./ddr_spd_reader
```

To successfully read the SPD data, an FTDI chip (FT4232H) is required, as well as a custom
board where to mount the memory modules, which exposes the I2C signal lines from the module's EEPROM.
