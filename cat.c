/*
 * es'hail WebSDR
 * ======================
 * by DJ0ABR 
 * 
 * cat.c ... Transceiver CAT interface
 * 
 * mainly handles the serial interfaceand routes commends to the CAT modules
 * 
 * */

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <stdlib.h>
#include <stdint.h>
#include <wchar.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <sys/file.h>
#include <pthread.h>
#include "cat.h"
#include "civ.h"

void closeSerial();
int activate_serial(char *serdevice, int baudrate);
void openSerialInterface();
int read_port();
void do_command();

void *catproc(void *pdata);
pthread_t cat_tid; 
int txmode = 1;
int trx_frequency = 0;

#define SERSPEED_CIV B4800
#define SERSPEED_DDS B115200

int fd_ser = -1; // handle of the ser interface
char serdevice[20] = {"/dev/ttyUSB0"};
int ser_command = 10;    // 0=no action 1=queryQRG 2=setPTT 3=releasePTT 4=setQRG

// creates a thread to run all serial specific jobs
// call this once after program start
// returns 0=failed, 1=ok
int cat_init()
{
    // automatically choose an USB port
    // start a new process
    int ret = pthread_create(&cat_tid,NULL,catproc, 0);
    if(ret)
    {
        printf("catproc NOT started\n");
        return 0;
    }
    
    printf("OK: catproc started\n");
    return 1;
}

void *catproc(void *pdata)
{
    pthread_detach(pthread_self());
    
    while(1)
    {
        if(txmode == 0)
        {
            // TX is OFF, no action
            sleep(1);
            continue;
        }
        
        if(txmode == 1)
        {
            // ICOM mode selected
            // open the serial interface and handle all messages through the CIV module
            if(fd_ser == -1)
            {
                // serial IF is closed, try to open it
                sleep(1);
                openSerialInterface(SERSPEED_CIV);
            }
            else
            {
                // serial IF is open, read one byte and pass it to the CIV message receiver routine
                int reti = read_port();
                if(reti == -1) 
                    usleep(10000);  // no data received
                else
                    readCIVmessage(reti);
            }
        }
 
        if(ser_command) do_command();   // a process requested a CAT action
    }
    
    closeSerial();
    pthread_exit(NULL);
}

void closeSerial()
{
    if(fd_ser != -1) close(fd_ser);
}

// Öffne die serielle Schnittstelle
int activate_serial(char *serdevice,int baudrate)
{
	struct termios tty;
    
	closeSerial();
	fd_ser = open(serdevice, O_RDWR | O_NDELAY);
	if (fd_ser < 0) {
		printf("error when opening %s, errno=%d\n", serdevice,errno);
		return -1;
	}

	if (tcgetattr(fd_ser, &tty) != 0) {
		printf("error %d from tcgetattr %s\n", errno,serdevice);
		return -1;
	}

	cfsetospeed(&tty, baudrate);
	cfsetispeed(&tty, baudrate);

	tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
	tty.c_iflag &= ~ICRNL; // binary mode (no CRLF conversion)
	tty.c_lflag = 0;

	tty.c_oflag = 0;
	tty.c_cc[VMIN] = 0; // 0=nonblocking read, 1=blocking read
	tty.c_cc[VTIME] = 0;

	tty.c_iflag &= ~(IXON | IXOFF | IXANY);

	tty.c_cflag |= (CLOCAL | CREAD);

	tty.c_cflag &= ~(PARENB | PARODD);
	tty.c_cflag |= CSTOPB;
	tty.c_cflag &= ~CRTSCTS;
    

	if (tcsetattr(fd_ser, TCSANOW, &tty) != 0) {
		printf("error %d from tcsetattr %s\n", errno, serdevice);
		return -1;
	}
	
	// set RTS/DTR
    int flags;
    ioctl(fd_ser, TIOCMGET, &flags);
    flags &= ~TIOCM_DTR;
    flags &= ~TIOCM_RTS;
    ioctl(fd_ser, TIOCMSET, &flags);
    
	return 0;
}

void direct_ptt(int onoff)
{
    // set/reset RTS/DTR
    int flags;
    ioctl(fd_ser, TIOCMGET, &flags);
    if(onoff)
    {
        flags |= TIOCM_DTR;
        flags |= TIOCM_RTS;
    }
    else
    {
        flags &= ~TIOCM_DTR;
        flags &= ~TIOCM_RTS;
    }
    ioctl(fd_ser, TIOCMSET, &flags);
}

void openSerialInterface(int baudrate)
{
int ret = -1;
static int ttynum = 0;
    
    while(ret == -1)
    {
        ret = activate_serial(serdevice,baudrate);
        if(ret == 0) 
        {
            printf("%s is now OPEN\n",serdevice);
            sleep(1);
            break;
        }
        fd_ser = -1;
        sleep(1);
        if(++ttynum >= 4) ttynum = 0;
        sprintf(serdevice,"/dev/ttyUSB%d",ttynum);
    }
}

// read one byte non blocking
int read_port()
{
static unsigned char c;

    int rxlen = read(fd_ser, &c, 1);
    
    if(rxlen == 0)
    {
        return -1;
    }

	return (unsigned int)c;
}

// schreibe ein
void write_port(unsigned char *data, int len)
{
    int ret = write(fd_ser, data, len);
    if(ret == -1)
    {
        fd_ser = -1;
    }
}

// execute a CAT command
void do_command()
{
    if(txmode == 0)
    {
        // no action
    }
    
    if(txmode == 1)
    {
        switch (ser_command)
        {
            case 1: civ_queryQRG();
                    break;
            case 4: civ_setQRG(trx_frequency);
                    break;
        }
    }

    ser_command = 0;
}
