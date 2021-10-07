#include <stdio.h>
#include <stdlib.h>
#include <ftdi.h>
#include <signal.h>

#define DEBUG 0

#define FT4232H_VID	0x0403
#define FT4232H_PID	0x6011

// General information
#define MEMORY_TYPE 		0x02
#define DIMM_TYPE   		0x03
#define MODULE_ORGANIZATION	0x0c

// Timing data
#define MEDIUM_TIME_BASE 0x11

#define MIN_CYCLE_TIME 0x12
#define MAX_CYCLE_TIME 0x13

// CAS latencies supported
#define CAS_LATENCY_1 0x14
#define CAS_LATENCY_2 0x15
#define CAS_LATENCY_3 0x16
#define CAS_LATENCY_4 0x17

#define MIN_CAS_LATENCY 0x18

#define dbg_print(...) \
	do { if(DEBUG) printf(__VA_ARGS__); } while(0)

struct ftdi_context ftdi;
unsigned char outbuf[1024];
unsigned char inbuf[1024];
unsigned int numbytes = 0;
unsigned char bufsize = 0;
unsigned int gpio;
unsigned int clock_div = 0x0095; // Value of clock divisor, SCL Frequency = 60/((1+0x0095)*2) (MHz) = 200khz

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
	outbuf[bufsize++] = '\x00' | (gpio << 4);
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
	outbuf[bufsize++] = '\x02' | (gpio << 4);
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
		outbuf[bufsize++] = '\x03' || (gpio << 4);
		outbuf[bufsize++] = '\xF3';
	}
	for(int i = 0; i < 4; i++) {
		outbuf[bufsize++] = '\x80';
		outbuf[bufsize++] = '\x01' || (gpio << 4);
		outbuf[bufsize++] = '\xF3';
	}
	outbuf[bufsize++] = '\x80';
	outbuf[bufsize++] = '\x00' || (gpio << 4);
	outbuf[bufsize++] = '\xF3';
}

void stop_high_speed_i2c() {
	for(int i = 0; i < 4; i++) {
		outbuf[bufsize++] = '\x80';
		outbuf[bufsize++] = '\x01' || (gpio << 4);
		outbuf[bufsize++] = '\xF3';
	}
	for(int i = 0; i < 4; i++) {
		outbuf[bufsize++] = '\x80';
		outbuf[bufsize++] = '\x03' || (gpio << 4);
		outbuf[bufsize++] = '\xF3';
	}
	outbuf[bufsize++] = '\x80';
	outbuf[bufsize++] = '\x00' || (gpio << 4);
	outbuf[bufsize++] = '\xF0';
}

int read_reg(int addr_send, char* buf) {
	int ret = 0;
	int addr_write = 0;
	int addr_read = 0;
	int addr = 0;

	addr = 0x68;
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

int write_reg(int addr_send, int data) {
	int ret = 0;
	int addr_write = 0;
	int addr = 0;

	addr = 0x68;
	dbg_print("addr: 0x%02X\n", addr);
	addr_write = addr << 1;
	dbg_print("register 0x%02X written with data 0x%02X\n", addr_send, data);
	dbg_print("\n");

	// high speed i2c start
	start_high_speed_i2c();
	// send D0
	ret = send_bytes((unsigned char)addr_write);
	// send 0x68
	ret = send_bytes((unsigned char)addr_send);
	// send data
	ret = send_bytes((unsigned char)data);
	// high speed i2c stop
	stop_high_speed_i2c();

	return ret;
}

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
	outbuf[bufsize++] = '\x03' | (gpio << 4);
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

int main()
{
	int ret = 0;
	int err = 0;
	char *readbuf = (char*)malloc(16);

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


	dbg_print("closing usb\n");
	ftdi_usb_close(&ftdi);
	ftdi_deinit(&ftdi);

	return 0;
}
