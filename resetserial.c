#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <termios.h>

int main(int argc, char **argv)
{
	int fd, flags;
	int delay = 10000;
	struct termios termios;

	if(argc != 2)
	{
		fprintf(stderr, "usage: resetserial <device>\n");
		exit(1);
	}

	if((fd = open(argv[1], O_RDONLY | O_NOCTTY, 0)) < 0)
	{
		perror("open");
		exit(1);
	}

	if(tcgetattr(fd, &termios))
	{
		perror("tcgetattr");
		exit(1);
	}

	cfmakeraw(&termios);

	termios.c_cflag |= CLOCAL;
	termios.c_cflag &= ~HUPCL;

	if(tcsetattr(fd, TCSANOW, &termios))
	{
		perror("tcsetattr");
		exit(1);
	}

	if(ioctl(fd, TIOCMGET, &flags))
	{
		perror("ioctl 1\n");
		exit(1);
	}

	// reset

	flags &= ~TIOCM_RTS;

	if(ioctl(fd, TIOCMSET, &flags))
	{
		perror("ioctl 2\n");
		exit(1);
	}

	usleep(delay);

	// gpio0

	flags &= ~TIOCM_DTR;

	if(ioctl(fd, TIOCMSET, &flags))
	{
		perror("ioctl 3\n");
		exit(1);
	}

	usleep(delay);

	// !reset

	flags |= TIOCM_RTS;

	if(ioctl(fd, TIOCMSET, &flags))
	{
		perror("ioctl 4\n");
		exit(1);
	}

	usleep(delay);

	flags |= TIOCM_DTR;

	if(ioctl(fd, TIOCMSET, &flags))
	{
		perror("ioctl 5\n");
		exit(1);
	}

	exit(0);
}
