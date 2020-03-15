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
	unsigned int mode = 0;

	if((argc != 2) && (argc != 3))
	{
		fprintf(stderr, "usage: resetserial <device> [mode]\n");
		fprintf(stderr, "       mode = 0: default\n");
		fprintf(stderr, "       mode = 1: reset RTS+DTR\n");
		fprintf(stderr, "       mode = 2: reset DTR, set RTS\n");
		fprintf(stderr, "       mode = 3: reset RTS, set DTR\n");
		fprintf(stderr, "       mode = 4: set RTS+DTR\n");
		exit(1);
	}

	if(argc == 3)
		mode = strtoul(argv[2], (char **)0, 0);

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

	switch(mode)
	{
		case(0):
		{

			// set reset

			flags &= ~TIOCM_RTS;

			if(ioctl(fd, TIOCMSET, &flags))
			{
				perror("ioctl 2\n");
				exit(1);
			}

			usleep(delay);

			// set gpio0

			if(mode)
				flags |= TIOCM_DTR;
			else
				flags &= ~TIOCM_DTR;

			if(ioctl(fd, TIOCMSET, &flags))
			{
				perror("ioctl 3\n");
				exit(1);
			}

			usleep(delay);

			// release reset

			flags |= TIOCM_RTS;

			if(ioctl(fd, TIOCMSET, &flags))
			{
				perror("ioctl 4\n");
				exit(1);
			}

			usleep(delay);

			// release gpio0

			flags |= TIOCM_DTR;

			if(ioctl(fd, TIOCMSET, &flags))
			{
				perror("ioctl 5\n");
				exit(1);
			}

			break;
		}

		case(1):
		{
			flags &= ~TIOCM_RTS;
			flags &= ~TIOCM_DTR;

			if(ioctl(fd, TIOCMSET, &flags))
			{
				perror("ioctl 12\n");
				exit(1);
			}

			break;
		}

		case(2):
		{
			flags |= TIOCM_RTS;
			flags &= ~TIOCM_DTR;

			if(ioctl(fd, TIOCMSET, &flags))
			{
				perror("ioctl 22\n");
				exit(1);
			}

			break;
		}

		case(3):
		{
			flags &= ~TIOCM_RTS;
			flags |= TIOCM_DTR;

			if(ioctl(fd, TIOCMSET, &flags))
			{
				perror("ioctl 32\n");
				exit(1);
			}

			break;
		}

		case(4):
		{
			flags |= TIOCM_RTS;
			flags |= TIOCM_DTR;

			if(ioctl(fd, TIOCMSET, &flags))
			{
				perror("ioctl 42\n");
				exit(1);
			}

			break;
		}
	}

	exit(0);
}
