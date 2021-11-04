/*
 * Copyright (C) 2021 Antmicro
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <ftdi.h>
#include <signal.h>

/*
 * RAM DIMMs SPD data reader
 *
 * The reader makes use of FTDI to send/receive data to/from the RDIMM module
 * by means of the I2C protocol.
 *
 * The byte decoding part is currently only targeting the DDR4 RAM type, but the SPD
 * can be captured potentially for any RAM type.
 */

#define DEBUG 0

// FTDI module definitions
#define FT4232H_VID	0x0403
#define FT4232H_PID	0x6011

// General information
#define MEMORY_TYPE 0x02
#define MODULE_TYPE 0x03

// Geometry information
#define BANK_BITS           0x04
#define COL_BITS            0x05
#define SDRAM_OPTIONS       0x06
#define MODULE_ORGANIZATION	0x0C
#define BUS_INFO	        0x0D

// Timing data
#define TIME_BASES 0x11

// Cycle time
#define MTB_MIN_CYCLE_TIME 0x12
#define MTB_MAX_CYCLE_TIME 0x13
#define FTB_MIN_CYCLE_TIME 0x7D
#define FTB_MAX_CYCLE_TIME 0x7C

// CAS latencies supported
#define CAS_LATENCY 0x14

#define MTB_TAA_MIN 0x18
#define MTB_TRCD_MIN 0x19
#define MTB_TRP_MIN 0x1A
#define FTB_TAA_MIN 0x7B
#define FTB_TRCD_MIN 0x7A
#define FTB_TRP_MIN 0x79

#define TRAS_RC 0x1B
#define TRAS_MIN 0x1C
#define MTB_TRC_MIN 0x1D
#define FTB_TRC_MIN 0x78

#define MTB_TRRD_S 0x26
#define MTB_TRRD_L 0x27
#define MTB_TCCD_L 0x28
#define FTB_TRRD_S 0x77
#define FTB_TRRD_L 0x76
#define FTB_TCCD_L 0x75

#define TRFC1_LSB 0x1E
#define TRFC1_MSB 0x1F
#define TRFC2_LSB 0x20
#define TRFC2_MSB 0x21
#define TRFC4_LSB 0x22
#define TRFC4_MSB 0x23

#define TFAW_MSB 0x24
#define TFAW_LSB 0x25

#define TWR_MSB 0x29
#define TWR_LSB 0x2A
#define TWTR 0x2B
#define TWTR_S 0x2C
#define TWTR_L 0x2D

#define dbg_print(...) \
    do { if(DEBUG) printf(__VA_ARGS__); } while(0)

struct ftdi_context ftdi;
unsigned char outbuf[1024];
unsigned char inbuf[1024];
unsigned int numbytes = 0;
unsigned char bufsize = 0;
unsigned int clock_div = 0x0095; // Value of clock divisor, SCL Frequency = 60/((1+0x0095)*2) (MHz) = 200khz

// Constant recurring string to report and undefined or not relevant information
char* UDEF = "Undefined";

// MTB: medium time base
// FTP: fine time base
//
// Both are expressed in picoseconds
int mtb = 125;
int ftb = 1;

int send_bytes(unsigned char data) {

    int ret = 0;
    unsigned int bytes_sent = 0;
    unsigned int bytes_read = 0;

    dbg_print("Sending bytes\n");
    dbg_print("bufsize: %d\n", bufsize);
    outbuf[bufsize++] = '\x11'; //FALLING_EDGE_CLOCK_BYTE_OUT;
    outbuf[bufsize++] = '\x00';
    outbuf[bufsize++] = '\x00';
    outbuf[bufsize++] = data;
    outbuf[bufsize++] = '\x80';
    outbuf[bufsize++] = '\x00';
    outbuf[bufsize++] = '\xF1';
    outbuf[bufsize++] = '\x22'; //RISING_EDGE_CLOCK_BIT_IN;
    outbuf[bufsize++] = '\x00';
    outbuf[bufsize++] = '\x87';

    dbg_print("bufsize: %d\n", bufsize);
    bytes_sent = ftdi_write_data(&ftdi, outbuf, bufsize);
    dbg_print("bytes sent: %u | %s:%d\n", bytes_sent, __func__, __LINE__);
    bufsize = 0;
    dbg_print("bufsize: %d\n", bufsize);

    /* check ACK */
    bytes_read = ftdi_read_data(&ftdi, inbuf, 1);
    dbg_print("bytes read: %u | %s:%d\n", bytes_read, __func__, __LINE__);
    if(!bytes_read) {
        printf("reading ack failed | %s:%d\n", __func__, __LINE__);
        return -1;
    }
    else if(inbuf[0] & 0x1) //checking ack
        ret = 1;

    dbg_print("bufsize: %d\n", bufsize);
    outbuf[bufsize++] = '\x80';
    outbuf[bufsize++] = '\x02';
    outbuf[bufsize++] = '\xF3';
    dbg_print("bufsize: %d\n", bufsize);

    return ret;
}

void read_bytes(char* readbuf, unsigned int readbufsize) {
    unsigned int clock = 60 * 1000/(1+clock_div)/2; // K Hz
    const int loop_count = (int)(10 * ((float)200/clock));

    dbg_print("read length: %u\n", readbufsize);

    outbuf[bufsize++] = '\x80';
    outbuf[bufsize++] = '\x00';
    outbuf[bufsize++] = '\x11';
    outbuf[bufsize++] = '\x24'; //FALLING_EDGE_CLOCK_BYTE_IN
    outbuf[bufsize++] = '\x00';
    outbuf[bufsize++] = '\x00';
    for(int i = 0; i < loop_count; i++) {
        outbuf[bufsize++] = '\x80';
        outbuf[bufsize++] = '\x02';
        outbuf[bufsize++] = '\x13';
    }
    for(int i = 0; i < loop_count; i++) {
        outbuf[bufsize++] = '\x80';
        outbuf[bufsize++] = '\x03';
        outbuf[bufsize++] = '\x13';
    }
    for(int i = 0; i < loop_count; i++) {
        outbuf[bufsize++] = '\x80';
        outbuf[bufsize++] = '\x02';
        outbuf[bufsize++] = '\x13';
    }
    numbytes = ftdi_write_data(&ftdi, outbuf, bufsize);
    dbg_print("sent %d bytes\n", numbytes);
    bufsize = 0;

    numbytes = ftdi_read_data(&ftdi, readbuf, readbufsize);
    dbg_print("read %d bytes\n", numbytes);
    if(numbytes < 1)
        dbg_print("reading bytes failed\n");
}

void start_high_speed_i2c() {
    for(int i = 0; i < 4; i++) {
        outbuf[bufsize++] = '\x80';
        outbuf[bufsize++] = '\x03';
        outbuf[bufsize++] = '\xF3';
    }
    for(int i = 0; i < 4; i++) {
        outbuf[bufsize++] = '\x80';
        outbuf[bufsize++] = '\x01';
        outbuf[bufsize++] = '\xF3';
    }
    outbuf[bufsize++] = '\x80';
    outbuf[bufsize++] = '\x00';
    outbuf[bufsize++] = '\xF3';
}

void stop_high_speed_i2c() {
	for(int i = 0; i < 4; i++) {
		outbuf[bufsize++] = '\x80';
		outbuf[bufsize++] = '\x01';
		outbuf[bufsize++] = '\xF3';
	}
	for(int i = 0; i < 4; i++) {
		outbuf[bufsize++] = '\x80';
		outbuf[bufsize++] = '\x03';
		outbuf[bufsize++] = '\xF3';
	}
	outbuf[bufsize++] = '\x80';
	outbuf[bufsize++] = '\x00';
	outbuf[bufsize++] = '\xF0';
}

int read_reg(int addr_send, char* buf) {
    int ret = 0;
    int addr_write = 0;
    int addr_read = 0;
    int addr = 0;

    addr = 0x50;
    dbg_print("addr: 0x%02X\n", addr);
    addr_write = addr << 1;
    addr_read = addr_write | 0x01;
    dbg_print("addr to read: 0x%02X\n", addr_send);

    // high speed i2c start
    start_high_speed_i2c();
    // send bytes and check ack - addr
    dbg_print("bufsize: %d\n", bufsize);
    dbg_print("addr_write: 0x%02X\n", addr_write);
    ret = send_bytes((unsigned char)addr_write);
    dbg_print("ret: %d\n", ret);
    dbg_print("bufsize: %d\n", bufsize);
    // send bytes and check ack - cmd
    dbg_print("addr_send: 0x%02X\n", addr_send);
    ret = send_bytes((unsigned char)addr_send);
    dbg_print("ret: %d\n", ret);
    dbg_print("bufsize: %d\n", bufsize);
    // high speed i2c start
    start_high_speed_i2c();
    dbg_print("bufsize: %d | %s:%d\n", bufsize, __func__, __LINE__);
    // send bytes and check ack - addr
    dbg_print("addr_read: 0x%02X\n", addr_read);
    ret = send_bytes((unsigned char)addr_read);
    dbg_print("ret: %d\n", ret);
    dbg_print("bufsize: %d | %s:%d\n", bufsize, __func__, __LINE__);
    // read bytes
    read_bytes(buf, 1);
    dbg_print("bytes read: 0x%02X\n", buf[0]);
    dbg_print("\n");
    // high speed i2c stop
    stop_high_speed_i2c();

    return ret;
}

// Initializes the FTDI module
void initialization(void) {

    int err = 0;
    /* Initialization */

    err |= ftdi_usb_reset(&ftdi);
    err |= ftdi_usb_purge_rx_buffer(&ftdi);
    err |= ftdi_usb_purge_tx_buffer(&ftdi);
    ftdi_set_bitmode(&ftdi, 0xFF, BITMODE_RESET);
    ftdi_set_bitmode(&ftdi, 0xFF, BITMODE_MPSSE);

    outbuf[bufsize++] = '\xAA';

    numbytes = ftdi_write_data(&ftdi, outbuf, bufsize);
    dbg_print("sent bytes: %u\n", numbytes);
    bufsize = 0;

    do {
        numbytes = ftdi_read_data(&ftdi, inbuf, 2);
        dbg_print("read bytes: %u\n", numbytes);
        if(numbytes < 2) {
            dbg_print("%s\n", ftdi_get_error_string(&ftdi));
            break;
        }
        dbg_print("bytes read: %02X %02X\n", inbuf[0], inbuf[1]);
    } while (numbytes == 0);

    outbuf[bufsize++] = '\x8A';
    outbuf[bufsize++] = '\x97';
    outbuf[bufsize++] = '\x8D';
    numbytes = ftdi_write_data(&ftdi, outbuf, bufsize);
    bufsize = 0;
    outbuf[bufsize++] = '\x80';
    outbuf[bufsize++] = '\x03';
    outbuf[bufsize++] = '\xF3';
    outbuf[bufsize++] = '\x86';
    outbuf[bufsize++] = clock_div & '\xFF';
    outbuf[bufsize++] = (clock_div >> 8) & '\xFF';
    numbytes = ftdi_write_data(&ftdi, outbuf, bufsize);
    bufsize = 0;
    outbuf[bufsize++] = '\x85';
    numbytes = ftdi_write_data(&ftdi, outbuf, bufsize);
    bufsize = 0;

    /*end of initialization - TODO: check if we get 0xFA 0xAA (echoed command + 0xAA)*/
}

/*
 * Utility functions to decode the bytes received from SPD
 */
void new_line() {
    printf("\n");
}

int power(int num) {
    return 1 << num;
}

int shift(char byte, int nbits, int nshift) {
    int mask = power(nbits) - 1;
    return (byte & (mask << nshift)) >> nshift;
}

int complement(char byte) {
    int res = byte & 0xFF;
    return byte & 0x80 ? res - 0x100 : res;
}

double get_timing(char mtb_byte, char ftb_byte) {
    int mtb_int = mtb_byte & 0xFF;

    // The FTB byte is in two's complement, we need to make it unsigned first
    int ftb_int = complement(ftb_byte);

    return (double) (mtb_int * mtb + ftb_int * ftb) / 1000;

}

void print_geometry(char *data) {
    uint8_t bank_byte = data[BANK_BITS] & 0XFF;
    uint8_t col_byte = data[COL_BITS] & 0XFF;

    int groups = power(shift(bank_byte, 2, 6));
    int group_banks = power(shift(bank_byte, 2, 4) + 2);
    int banks = groups * group_banks;
    int rows = shift(col_byte, 3, 3) + 12;
    int cols = shift(col_byte, 3, 0) + 9;

    printf("Num Banks: %d (groups = %d, group banks = %d)\n", banks, groups, group_banks);
    printf("Num Rows: %d (bits: %d)\n", power(rows), rows);
    printf("Num Cols: %d (bits: %d)\n", power(cols), cols);

    int org_byte = data[MODULE_ORGANIZATION] & 0xFF;
    int dram_width = 4 << (org_byte & 0x07);
    int ranks = shift(org_byte, 3, 3) + 1;
    printf("Num Ranks: %d\n", ranks);
    new_line();


    int opt_byte = data[SDRAM_OPTIONS] & 0xFF;
    int signal_loading = opt_byte & 0x03;
    int num_dies = shift(opt_byte, 3, 4) + 1;

    int bus_width = 8 << (data[BUS_INFO] & 0x07);

    // Single die capacity in MB
    int capacity = (256 << (bank_byte & 0x07)) / 8;
    capacity *= bus_width / dram_width;
    capacity *= ranks;
    capacity *= signal_loading == 2 ? num_dies : 1;

    printf("Total RAM capacity: %d MBs", capacity);
}

void print_memory_type(char *data) {
    uint8_t content = data[MEMORY_TYPE] & 0xFF;
    const char* mem_types[] = {
        UDEF, UDEF, UDEF, UDEF,                            // 0, 1, 2, 3
        "SDR SDRAM", UDEF, UDEF, "DDR SDRAM",              // 4, 5, 6, 7
        "DDR2 SDRAM", UDEF, UDEF, "DDR3 SDRAM",            // 8, 9, 10, 11
        "DDR4 SDRAM", UDEF, UDEF, "LPDDR3 SDRAM",          // 12, 13, 14, 15
        "LPDDR4 SDRAM", UDEF, "DDR5 SDRAM", "LPDDR5 SDRAM" // 16, 17, 18, 19
    };

    printf("Memory type: %s\n", mem_types[content]);
}

void print_module_type_ddr4(char *data) {
    uint8_t content = data[MODULE_TYPE] & 0x07;
    const char* module_types[] = {
        UDEF, "RDIMM", "UDIMM", "SO-DIMM",
        "LRDIMM", UDEF, UDEF, UDEF,
    };

    printf("Module type: %s\n", module_types[content]);
}

int main()
{
    int err = 0;
    char *readbuf = (char*)malloc(16);
    char data[128];

    dbg_print("ftdi init:\n");

    err = ftdi_init(&ftdi);
    if(err) {
        printf("ftdi init failed\n");
        return 1;
    }

    err = ftdi_usb_open(&ftdi, FT4232H_VID, FT4232H_PID);
    if(err) {
        printf("usb open failed : %s\n", ftdi_get_error_string(&ftdi));
        return 1;
    }

    /* Initialization */
    initialization();

    dbg_print("bufsize: %d\n", bufsize);

    for (int i = 0; i < 128; i++) {
        read_reg(i & 0xFF, readbuf);
        data[i] = *readbuf;
    }

    dbg_print("closing usb\n");
    ftdi_usb_close(&ftdi);
    ftdi_deinit(&ftdi);

    uint8_t content = data[TIME_BASES] & 0x03;
    if (content != 0x00) {
        printf("ERROR: wrong time bases configuration!\n");
        return 1;
    }

    printf("Basic Memory Information\n");
    printf("========================\n");

    print_memory_type(data);
    print_module_type_ddr4(data);
    double min_cycle_time = get_timing(data[MTB_MIN_CYCLE_TIME], data[FTB_MIN_CYCLE_TIME]);
    double max_cycle_time = get_timing(data[MTB_MAX_CYCLE_TIME], data[FTB_MAX_CYCLE_TIME]);
    printf("Minimum Cycle Time (tCKAVG min): %0.3f (ns)\n", min_cycle_time);
    printf("Maximum Cycle Time (tCKAVG max): %0.3f (ns)\n", max_cycle_time);
    printf("DDR speed: %d MT/s\n", (int) (2 * (1000 / min_cycle_time)));

    new_line();
    printf("Memory Geometry\n");
    printf("===============\n");
    print_geometry(data);

    new_line();
    new_line();
    printf("Timing Data\n");
    printf("===========\n");
    double taa = get_timing(data[MTB_TAA_MIN], data[FTB_TAA_MIN]);
    double trcd = get_timing(data[MTB_TRCD_MIN], data[FTB_TRCD_MIN]);
    double trp = get_timing(data[MTB_TRP_MIN], data[FTB_TRP_MIN]);
    printf("Minimum CAS latency time (tAA min): %0.3f (ns)\n", taa);
    printf("Minimum RAS to CAS delay time (tRCD min): %0.3f (ns)\n", trcd);
    printf("Minimum row precharge delay time (tRP min): %0.3f (ns)\n", trp);

    uint32_t cas_latencies = ((data[CAS_LATENCY+3] & 0x3F) << 24) +
                             ((data[CAS_LATENCY+2] & 0xFF) << 16) +
                             ((data[CAS_LATENCY+1] & 0xFF) << 8) +
                             (data[CAS_LATENCY] & 0xFF);
    int cas_start = (data[CAS_LATENCY+3] & 0x80) ? 23 : 7;

    printf("CAS latencies supported:\n");
    for (int i = 0; i < 30; i++) {
        if (cas_latencies & (1 << i)) {
            printf("  - %d\n", cas_start + i);
        }
    }

    // TRAS - TRC
    new_line();
    int tras_mtb = (int)((data[TRAS_RC] & 0x0F) << 8) + (int)(data[TRAS_MIN] & 0XFF);
    double tras = (double) (tras_mtb * mtb) / 1000;
    printf("Minimum active to precharge delay time (tRAS min): %0.3f (ns)\n", tras);

    int trc_mtb = (int)((data[TRAS_RC] & 0xF0) << 4) + (int)(data[MTB_TRC_MIN] & 0XFF);
    int trc_ftb = complement(data[FTB_TRC_MIN]);
    double trc = (double) (trc_mtb * mtb + trc_ftb * ftb) / 1000;
    printf("Minimum active to active/refresh delay time (tRC min): %0.3f (ns)\n", trc);

    // RRD_[SL] and CCD
    new_line();
    double trrd_s = get_timing(data[MTB_TRRD_S], data[FTB_TRRD_S]);
    double trrd_l = get_timing(data[MTB_TRRD_L], data[FTB_TRRD_L]);
    double tccd_l = get_timing(data[MTB_TCCD_L], data[FTB_TCCD_L]);
    printf("Minimum activate to activate delay time (tRRD_S) min): %0.3f (ns)\n", trrd_s);
    printf("Minimum activate to activate delay time (tRRD_L) min): %0.3f (ns)\n", trrd_l);
    printf("Minimum CAS to CAS delay time (tCCD_L) min): %0.3f (ns)\n", tccd_l);

    // Refresh recovery delay
    new_line();
    int trfc1_mtb = (int)((data[TRFC1_MSB] & 0XFF) << 8) + (int)(data[TRFC1_LSB] & 0XFF);
    double trfc1 = (double) (trfc1_mtb * mtb) / 1000;
    printf("Minimum refresh recovery delay time (tRFC1 min): %0.3f (ns)\n", trfc1);
    int trfc2_mtb = (int)((data[TRFC2_MSB] & 0XFF) << 8) + (int)(data[TRFC2_LSB] & 0XFF);
    double trfc2 = (double) (trfc2_mtb * mtb) / 1000;
    printf("Minimum refresh recovery delay time (tRFC2 min): %0.3f (ns)\n", trfc2);
    int trfc4_mtb = (int)((data[TRFC4_MSB] & 0XFF) << 8) + (int)(data[TRFC4_LSB] & 0XFF);
    double trfc4 = (double) (trfc4_mtb * mtb) / 1000;
    printf("Minimum refresh recovery delay time (tRFC4 min): %0.3f (ns)\n", trfc4);

    // Four activate window delay
    new_line();
    int tfaw_mtb = (int)((data[TFAW_MSB] & 0x0F) << 8) + (int)(data[TFAW_LSB] & 0XFF);
    double tfaw = (double) (tfaw_mtb * mtb) / 1000;
    printf("Minimum four activate window delay time (tFAW min): %0.3f (ns)\n", tfaw);

    // Recovery time
    new_line();
    int twr_mtb = (int)((data[TWR_MSB] & 0x0F) << 8) + (int)(data[TWR_LSB] & 0XFF);
    double twr = (double) (twr_mtb * mtb) / 1000;
    printf("Minimum write recovery time (tWR min): %0.3f (ns)\n", twr);

    // Write to Read time
    new_line();
    int twtr_s_mtb = (int)((data[TWTR] & 0x0F) << 8) + (int)(data[TWTR_S] & 0XFF);
    double twtr_s = (double) (twtr_s_mtb * mtb) / 1000;
    printf("Minimum write to read time (tWTR_S min): %0.3f (ns)\n", twtr_s);
    int twtr_l_mtb = (int)((data[TWTR] & 0xF0) << 4) + (int)(data[TWTR_L] & 0XFF);
    double twtr_l = (double) (twtr_l_mtb * mtb) / 1000;
    printf("Minimum write to read time (tWTR_L min): %0.3f (ns)\n", twtr_l);
}
