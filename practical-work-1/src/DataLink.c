#include "DataLink.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

const int FALSE = 0;
const int TRUE = 1;
const int BAUDRATE = B38400;
const int FLAG = 0x7E;
const int A = 0x03;
const int C = 0x03;
const int DISC = 0x0B;

typedef enum {
	START, FLAG_RCV, A_RCV, C_RCV, BCC_OK, STOP
} State;

int dataLink(const char* port, ConnnectionMode mode) {
	int fd = openSerialPort(port);

	struct termios oldtio, newtio;
	saveCurrentPortSettingsAndSetNewTermios(mode, fd, &oldtio, &newtio);

	int portfd = llopen(port, mode);

	printf("\nStarting llclose\n");

	int closeResult = llclose(portfd, mode);

	printf("Result from llclose: %d\n", closeResult);

	if (tcsetattr(fd, TCSANOW, &oldtio) == -1) {
		perror("tcsetattr");
		exit(-1);
	}

	close(fd);

	return 0;
}

int openSerialPort(const char* port) {
	// Open serial port device for reading and writing and not as controlling
	// tty because we don't want to get killed if linenoise sends CTRL-C.
	int fd = open(port, O_RDWR | O_NOCTTY);

	if (fd < 0) {
		perror(port);
		exit(-1);
	}

	return fd;
}

void saveCurrentPortSettingsAndSetNewTermios(ConnnectionMode mode, int fd,
		struct termios* oldtio, struct termios* newtio) {
	saveCurrentPortSettings(fd, oldtio);
	setNewTermios(mode, fd, newtio);
}

void saveCurrentPortSettings(int fd, struct termios* oldtio) {
	if (tcgetattr(fd, oldtio) == -1) {
		perror("tcgetattr");
		exit(-1);
	}
}

void setNewTermios(ConnnectionMode mode, int fd, struct termios* newtio) {
	bzero(newtio, sizeof(*newtio));
	newtio->c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
	newtio->c_iflag = IGNPAR;
	newtio->c_oflag = 0;
	newtio->c_lflag = 0;

	switch (mode) {
	case SEND: {
		// inter-character timer unused
		newtio->c_cc[VTIME] = 30;

		// blocking read until x chars received
		newtio->c_cc[VMIN] = 0;

		break;
	}
	case RECEIVE: {
		// inter-character timer unused
		newtio->c_cc[VTIME] = 0;

		// blocking read until x chars received
		newtio->c_cc[VMIN] = 1;

		break;
	}
	default:
		break;
	}

	tcflush(fd, TCIOFLUSH);
	if (tcsetattr(fd, TCSANOW, newtio) == -1) {
		perror("tcsetattr");
		exit(-1);
	} else
		printf("New termios structure set.\n");
}

int llopen(const char* port, ConnnectionMode mode) {
	int fd = openSerialPort(port);
	if (fd < 0)
		return fd;

	unsigned int bufSize = 5;
	unsigned char buf[bufSize];

	switch (mode) {
	case SEND: {
		int try, numTries = 4;

		for (try = 0; try < numTries; try++) {
			createSETBuf(buf, bufSize);
			send(fd, buf, bufSize);

			// TODO work this out
			// llread();

			if (receive(fd, buf, bufSize)) {
				printBuf(buf);
				break;
			} else {
				if (try == numTries - 1) {
					printf("Connection aborted.\n");
					return -1;
				} else
					printf("Time out!\n\nRetrying: ");
			}
		}

		break;
	}
	case RECEIVE: {
		receive(fd, buf, bufSize);
		printBuf(buf);
		// TODO work this out
		// llwrite();

		send(fd, buf, bufSize);
		// TODO work this out
		// llread();

		break;
	}
	default:
		break;
	}

	return fd;
}

int llread() {
	return 1;
}

int llwrite() {
	return 1;
}

int llclose(int fd, ConnnectionMode mode) {
	int closeResult = -1;
	unsigned int bufSize = 5;
	unsigned char buf[bufSize];

	switch (mode) {
	case SEND: {
		buf[0] = DISC;
		if (send(fd, buf, bufSize)) {

			if (receiveDISC(fd, buf, bufSize)) {
				printDISC(buf);
				createSETBuf(buf, bufSize);

				if (send(fd, buf, bufSize)) {
					closeResult = close(fd);
					printf("Serial Port is closed.\n");
				} else {
					printf("ERROR: UA was not sent.\n");
					printf("       Cannot close serial port!\n");
					closeResult = -1;
				}

			} else {
				printf("ERROR: DISC was not received.\n");
				closeResult = -1;
			}

		} else {
			printf("ERROR: DISC was not sent.\n");
			closeResult = -1;
		}
		break;
	}
	case RECEIVE: {
		bufSize = 1;
		printf("Reading from serial port.\n");
		if (receiveDISC(fd, buf, bufSize)) {
			printDISC(buf);

			if (send(fd, buf, bufSize)) {
				bufSize = 5;

				if (receive(fd, buf, bufSize)) {
					printBuf(buf);
					closeResult = close(fd);
				} else {
					printf("ERROR: UA was not received.\n");
					printf("       Cannot close serial port!\n");
					closeResult = -1;
				}

			} else {
				printf("ERROR: DISC was not sent.\n");
				closeResult = -1;
			}

		} else {
			printf("ERROR: DISC was not received.\n");
			closeResult = -1;

		}
		break;
	}
	default:
		break;
	}

	return closeResult;
}

int send(int fd, unsigned char* buf, unsigned int bufSize) {
	printf("Sending to serial port.\n");

	if (write(fd, buf, bufSize * sizeof(*buf)) < 0) {
		printf("ERROR: unable to write.\n");
		return 0;
	}

	printf("OK!\n");

	return 1;
}

const int DEBUG_STATE_MACHINE = 0;

int receive(int fd, unsigned char* buf, unsigned int bufSize) {
	printf("Reading from serial port.\n");

	int numReadBytes;
	State state = START;

	volatile int done = FALSE;
	while (!done) {
		unsigned char c;

		if (state != STOP) {
			numReadBytes = read(fd, &c, 1);

			if (DEBUG_STATE_MACHINE) {
				printf("Number of bytes read: %d\n", numReadBytes);
				if (numReadBytes)
					printf("Read char: 0x%02x\n", c);
			}

			if (!numReadBytes) {
				printf("ERROR: nothing received.\n");
				return 0;
			}
		}

		switch (state) {
		case START:
			if (c == FLAG) {
				if (DEBUG_STATE_MACHINE)
					printf("START: FLAG received. Going to FLAG_RCV.\n");
				buf[START] = c;
				state = FLAG_RCV;
			}
			break;
		case FLAG_RCV:
			if (c == A) {
				if (DEBUG_STATE_MACHINE)
					printf("FLAG_RCV: A received. Going to A_RCV.\n");
				buf[FLAG_RCV] = c;
				state = A_RCV;
			} else if (c != FLAG)
				state = START;
			break;
		case A_RCV:
			if (c == C) {
				if (DEBUG_STATE_MACHINE)
					printf("A_RCV: C received. Going to C_RCV.\n");
				buf[A_RCV] = c;
				state = C_RCV;
			} else if (c == FLAG)
				state = FLAG_RCV;
			else
				state = START;
			break;
		case C_RCV:
			if (c == (A ^ C)) {
				if (DEBUG_STATE_MACHINE)
					printf("C_RCV: BCC received. Going to BCC_OK.\n");
				buf[C_RCV] = c;
				state = BCC_OK;
			} else if (c == FLAG)
				state = FLAG_RCV;
			else
				state = START;
			break;
		case BCC_OK:
			if (c == FLAG) {
				if (DEBUG_STATE_MACHINE)
					printf("BCC_OK: FLAG received. Going to STOP.\n");
				buf[BCC_OK] = c;
				state = STOP;
			} else
				state = START;
			break;
		case STOP:
			buf[STOP] = 0;
			done = TRUE;
			break;
		default:
			break;
		}
	}

	printf("OK!\n");

	return 1;
}

int receiveDISC(int fd, unsigned char* buf, unsigned int bufSize) {
	printf("Reading DISC ...\n");

	unsigned char c;
	unsigned int numReadBytes;

	cleanBuf(buf, bufSize);

	numReadBytes = read(fd, &c, 1);

	if (DEBUG_STATE_MACHINE) {
		printf("Number of bytes read: %d\n", numReadBytes);
		if (numReadBytes)
			printf("Read char: 0x%02x\n", c);
	}

	if (c == DISC) {
		buf[0] = c;
		return 1;
	}
	else
		return 0;
}

void createSETBuf(unsigned char* buf, unsigned int bufSize) {
	cleanBuf(buf, bufSize);

	buf[0] = FLAG;
	buf[1] = A;
	buf[2] = C;
	buf[3] = buf[1] ^ buf[2];
	buf[4] = FLAG;
}

void cleanBuf(unsigned char* buf, unsigned int bufSize) {
	memset(buf, 0, bufSize * sizeof(*buf));
}

void printBuf(unsigned char* buf) {
	printf("-------------------------\n");
	printf("- FLAG: 0x%02x\t\t-\n", buf[0]);
	printf("- A: 0x%02x\t\t-\n", buf[1]);
	printf("- C: 0x%02x\t\t-\n", buf[2]);
	printf("- BCC: 0x%02x = 0x%02x\t-\n", buf[3], buf[1] ^ buf[2]);
	printf("- FLAG: 0x%02x\t\t-\n", buf[4]);
	printf("-------------------------\n");
}
void printDISC(unsigned char* buf) {
	printf("-------------------------\n");
	printf("- DISC: 0x%02x\t\t-\n", buf[0]);
	printf("-------------------------\n");
}

