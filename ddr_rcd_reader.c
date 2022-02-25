/*
 * Copyright (C) 2022 Antmicro
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
#include <time.h>

/*
 * RAM DIMMs RCD data reader
 *
 * The reader makes use of FTDI to send/receive data to/from the RDIMM module
 * by means of the I2C protocol.
 *
 */

#define DEBUG 0

// FTDI module definitions
#define FT4232H_VID	0x0403
#define FT4232H_PID	0x6011

// FTDI module definitions
#define FT2232H_VID	0x0403
#define FT2232H_PID	0x6010

#define dbg_print(...) \
    do { if(DEBUG) printf(__VA_ARGS__); } while(0)

struct ftdi_context ftdi;
unsigned char outbuf[1024];
unsigned char inbuf[1024];
unsigned int numbytes = 0;
unsigned char bufsize = 0;
unsigned int clock_div = 0x0095; // Value of clock divisor, SCL Frequency = 60/((1+0x0095)*2) (MHz) = 200khz

char data[128];
int bc = 0;

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
    ftdi_write_data(&ftdi, outbuf, bufsize);
    bufsize = 0;
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
    ftdi_write_data(&ftdi, outbuf, bufsize);
    bufsize = 0;
}

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
    dbg_print("bytes sent: %u | %s:%d\n", data, __func__, __LINE__);
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

    for (int i = 0; i < 20; i++)
        dbg_print("inbuf %d: %u\n", i, inbuf[i]);

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

void send_cmd_data(int dev_addr_wr, int cmd, int data) {
    start_high_speed_i2c();
    // addr
    send_bytes((unsigned char)dev_addr_wr);

    // cmd
    send_bytes((unsigned char)cmd);

    // data
    send_bytes((unsigned char)data);
    stop_high_speed_i2c();
}

void receive_cmd_data(int dev_addr_wr, int dev_addr_rd, int cmd, char* buf) {
    start_high_speed_i2c();
    // addr
    send_bytes((unsigned char)dev_addr_wr);

    // cmd
    send_bytes((unsigned char)cmd);

    start_high_speed_i2c();
    // addr
    send_bytes((unsigned char)dev_addr_rd);

    // cmd
    read_bytes(buf, 1);
    stop_high_speed_i2c();
}

void read_regs(int addr_send) {
    int addr_write = 0;
    int addr_read = 0;
    int addr = 0;

    addr = 0x58; // Corresponding to 1011000 device address as per JEDEC specs
    addr_write = addr << 1;
    addr_read = addr_write | 0x01;

    // Implementing single byte transfers

    // Init read
    send_cmd_data(addr_write, 0x80, 0x00);
    send_cmd_data(addr_write, 0x00, 0xB0);
    send_cmd_data(addr_write, 0x00, 0x00);
    send_cmd_data(addr_write, 0x40, addr_send);

    // Receive data
    char *buf = (char*)malloc(16);
    receive_cmd_data(addr_write, addr_read, 0x80, buf);
    printf("%02x\n", *buf);

    receive_cmd_data(addr_write, addr_read, 0x00, buf);
    data[bc] = *buf;
    printf("%02x\n", data[bc++]);

    receive_cmd_data(addr_write, addr_read, 0x00, buf);
    data[bc] = *buf;
    printf("%02x\n", data[bc++]);

    receive_cmd_data(addr_write, addr_read, 0x00, buf);
    data[bc] = *buf;
    printf("%02x\n", data[bc++]);

    receive_cmd_data(addr_write, addr_read, 0x40, buf);
    data[bc] = *buf;
    printf("%02x\n", data[bc++]);
}

void write_reg_byte(int addr_send, int data) {
    int addr_write = 0;
    int addr = 0;

    addr = 0x58; // Corresponding to 1011000 device address as per JEDEC specs
    addr_write = addr << 1;

    // Implementing single byte transfers

    // Init write
    send_cmd_data(addr_write, 0x84, 0x00);
    send_cmd_data(addr_write, 0x04, 0xB0);
    send_cmd_data(addr_write, 0x04, 0x00);
    send_cmd_data(addr_write, 0x04, addr_send);

    // Write data
    send_cmd_data(addr_write, 0x44, data);
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

int main()
{
    int err = 0;

    dbg_print("ftdi init:\n");

    err = ftdi_init(&ftdi);
    if(err) {
        printf("ftdi init failed\n");
        return 1;
    }

    err = ftdi_usb_open(&ftdi, FT2232H_VID, FT2232H_PID);
    if(err) {
        printf("usb open failed : %s\n", ftdi_get_error_string(&ftdi));
        return 1;
    }

    /* Initialization */
    initialization();

#if 1
    // Soft reset
    write_reg_byte(0x0B, 0x00);

    // Drive strength
    write_reg_byte(0x09, 0x50);
    write_reg_byte(0x0A, 0x55);

    // RDIMM mode
    write_reg_byte(0x0E, 0x40);

    // Speed
    write_reg_byte(0x0D, 0x00);
    write_reg_byte(0x12, 0x12);
#endif

    for (int i = 0; i < 32; i += 4)
        read_regs(i);

    dbg_print("closing usb\n");
    ftdi_usb_close(&ftdi);
    ftdi_deinit(&ftdi);
}
