#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <poll.h>
#include <getopt.h>
#include <openssl/md5.h>
#include <sys/time.h>

static void usage(void)
{
	fprintf(stderr, "usage: otapush [options] host file\n");
	fprintf(stderr, "-c|--dont-commit      don't commit (reset and load new image)\n");
	fprintf(stderr, "-p|--port             set command port (default 24)\n");
	fprintf(stderr, "-s|--chunk-size 2^s   set command port (5 - 9, default 8 = 256 bytes)\n");
	fprintf(stderr, "-t|--timeout ms       set communication timeout (default = 30000 = 30s)\n");
	fprintf(stderr, "-v|--verify           verify (instead of write)\n");
}

static int resolve(const char * hostname, int port, struct sockaddr_in6 *saddr)
{
	struct addrinfo hints;
	struct addrinfo *res;
	char service[16];
	int s;

	snprintf(service, sizeof(service), "%u", port);
	memset(&hints, 0, sizeof(hints));

	hints.ai_family		=	AF_INET6;
	hints.ai_socktype	=	SOCK_STREAM;
	hints.ai_flags		=	AI_NUMERICSERV | AI_V4MAPPED;

	if((s = getaddrinfo(hostname, service, &hints, &res)))
	{
		freeaddrinfo(res);
		return(0);
	}

	*saddr = *(struct sockaddr_in6 *)res->ai_addr;
	freeaddrinfo(res);

	return(1);
}

static int read_timeout(int fd, void *dst, ssize_t size, int timeout)
{
	struct pollfd	pfd;

	pfd.fd		= fd;
	pfd.events	= POLLIN;

	if(poll(&pfd, 1, timeout) != 1)
		return(-1);

	return(read(fd, dst, size));
}

static int write_timeout(int fd, const void *src, ssize_t length, int timeout)
{
	struct pollfd	pfd;

	pfd.fd		= fd;
	pfd.events	= POLLOUT;

	if(poll(&pfd, 1, timeout) != 1)
		return(-1);

	return(write(fd, src, length));
}

static void bin_to_hex(unsigned int src_length, const char *src, unsigned int dst_size, char *dst)
{
	unsigned int src_ix, dst_ix;
	unsigned char current;

	for(src_ix = 0, dst_ix = 0; (src_ix < src_length) && ((dst_ix + 1) < dst_size); src_ix++, dst_ix += 2)
	{
		current = (unsigned char)src[src_ix];
		sprintf(&dst[dst_ix], "%02x", current);
	}

	dst[dst_ix] = '\0';
}

static void md5_hash_to_string(const char *hash, unsigned int size, char *string)
{
	unsigned int src, dst;
	unsigned int high, low;

	for(src = 0, dst = 0; (src < 16) && ((dst + 2) < size); src++)
	{
		high = (hash[src] & 0xf0) >> 4;

		if(high > 9)
			high = (high - 10) + 'a';
		else
			high = high + '0';

		string[dst++] = high;

		low = (hash[src] & 0x0f) >> 0;

		if(low > 9)
			low = (low - 10) + 'a';
		else
			low = low + '0';

		string[dst++] = low;
	}

	string[dst++] = '\0';
}

typedef struct
{
	const char *id;
	const char *cmd;
	const char *progress;
	const char *reply;
	const char *reply_ok;
} cmd_trait_t;

static cmd_trait_t cmd_trait[] =
{
	{
		"verify",
		"ov",
		"verified",
		"VERIFY",
		"VERIFY_OK"
	},

	{
		"write",
		"ow",
		"written",
		"WRITE",
		"WRITE_OK"
	}
};

int main(int argc, char * const *argv)
{
	int					file_fd, sock_fd, val;
	struct sockaddr_in6	saddr;
	const char			*hostname;
	const char			*filename;
	int					file_length, done;
	char				buffer[1024], hexbuf[2048], cmdbuf[4048];
	ssize_t				bufread;
	MD5_CTX				md5;
	char				md5_hash[MD5_DIGEST_LENGTH], md5_string[MD5_DIGEST_LENGTH * 2 + 1];
	int					arg;
	int					port;
	int					verify, dontcommit, chunk_size, timeout;
	int					rv = 1;
	struct timeval		start, now;
	int					seconds, useconds;
	double				duration, rate;
	int					slot, sector;
	cmd_trait_t			*trait;

	gettimeofday(&start, 0);

	dontcommit = 0;
	port = 24;
	chunk_size = 8;
	timeout = 30000;
	verify = 0;

	static const char *shortopts = "cp:s:t:v";
	static const struct option longopts[] =
	{
		{ "dont-commmit",	no_argument,		0, 'c' },
		{ "port",			required_argument,	0, 'p' },
		{ "chunk-size",		required_argument,	0, 's' },
		{ "timeout",		required_argument,	0, 't' },
		{ "verify",			no_argument,		0, 'v' },
		{ 0, 0, 0, 0 }
	};

	file_fd = -1;
	sock_fd = -1;

	while((arg = getopt_long(argc, argv, shortopts, longopts, 0)) != -1)
	{
		switch(arg)
		{
			case('c'):
			{
				dontcommit = 1;
				break;
			}

			case('p'):
			{
				port = atoi(optarg);
				break;
			}

			case('s'):
			{
				chunk_size = atoi(optarg);
				break;
			}

			case('t'):
			{
				timeout = atoi(optarg);
				break;
			}

			case('v'):
			{
				verify = 1;
				break;
			}
		}
	}

	if((argc - optind) < 2)
	{
		usage();
		exit(1);
	}

	if((chunk_size < 5) || (chunk_size > 9))
	{
		fprintf(stderr, "chunk size must be between 5 (32 bytes) and 9 (512 bytes)\n");
		exit(1);
	}

	hostname = argv[optind + 0];
	filename = argv[optind + 1];

	if(verify)
		trait = &cmd_trait[0];
	else
		trait = &cmd_trait[1];

	if((file_fd = open(filename, O_RDONLY, 0)) < 0)
	{
		fprintf(stderr, "cannot open file %s: %m\n", filename);
		goto error;
	}

	if((file_length = lseek(file_fd, 0, SEEK_END)) == -1)
	{
		fprintf(stderr, "file seek failed: %m\n");
		goto error;
	}

	if(lseek(file_fd, 0, SEEK_SET) == -1)
	{
		fprintf(stderr, "file seek failed: %m\n");
		goto error;
	}

	if(!resolve(hostname, port, &saddr))
	{
		fprintf(stderr, "cannot resolve hostname %s: %m\n", hostname);
		return(-1);
	}

	if((sock_fd = socket(AF_INET6, SOCK_STREAM, 0)) < 0)
	{
		fprintf(stderr, "socket failed: %m\n");
		goto error;
	}

	val = 1;

	if(setsockopt(sock_fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val)))
	{
		fprintf(stderr, "socket set nodelay failed: %m\n");
		goto error;
	}

	val = 1200;

	if(setsockopt(sock_fd, IPPROTO_TCP, TCP_WINDOW_CLAMP, &val, sizeof(val)))
	{
		fprintf(stderr, "socket set window clamp failed: %m\n");
		goto error;
	}

	if(connect(sock_fd, (const struct sockaddr *)&saddr, sizeof(saddr)))
	{
		fprintf(stderr, "connect failed: %m\n");
		goto error;
	}

	if((bufread = read_timeout(sock_fd, buffer, sizeof(buffer) - 1, timeout)) <= 0)
	{
		fprintf(stderr, "socket read failed: %m\n");
		goto error;
	}

	buffer[bufread] = '\0';

	if(strcmp(buffer, "OK\n"))
	{
		fprintf(stderr, "no response\n");
		goto error;
	}

	snprintf(cmdbuf, sizeof(cmdbuf), "%s %u\n", trait->cmd, file_length);

	if(write_timeout(sock_fd, cmdbuf, strlen(cmdbuf), timeout) != (ssize_t)strlen(cmdbuf))
	{
		fprintf(stderr, "command %s failed (%m)\n", trait->id);
		goto error;
	}

	if((bufread = read_timeout(sock_fd, buffer, sizeof(buffer) - 1, timeout)) <= 0)
	{
		fprintf(stderr, "command %s timeout: %m\n", trait->id);
		goto error;
	}

	buffer[bufread] = '\0';

	if(sscanf(buffer, "%16s %d %d\n", cmdbuf, &slot, &sector) != 3)
	{
		fprintf(stderr, "command %s failed: %s\n", trait->id, buffer);
		goto error;
	}

	if(strcmp(cmdbuf, trait->reply))
	{
		fprintf(stderr, "invalid %s response: %s\n", trait->id, buffer);
		goto error;
	}

	MD5_Init(&md5);

	fprintf(stderr, "starting %s of file: %s, length: %u, slot: %d, address: 0x%x\n", trait->id, filename, file_length, slot, sector * 0x1000);

	for(done = 0;;)
	{
		if((bufread = read(file_fd, buffer, 1 << chunk_size)) < 0)
		{
			fprintf(stderr, "\nfile read failed: %m\n");
			goto error;
		}

		if(bufread == 0)
			break;

		done += bufread;

		buffer[bufread] = '\0';

		MD5_Update(&md5, buffer, bufread);

		bin_to_hex(bufread, buffer, sizeof(hexbuf), hexbuf);

		snprintf(cmdbuf, sizeof(cmdbuf), "os %lu %s\n", bufread, hexbuf);

		if(write_timeout(sock_fd, cmdbuf, strlen(cmdbuf), timeout) != (ssize_t)strlen(cmdbuf))
		{
			fprintf(stderr, "\nsend failed (%m)\n");
			goto error;
		}

		if((bufread = read_timeout(sock_fd, buffer, sizeof(buffer), timeout)) <= 0)
		{
			fprintf(stderr, "\nsend acknowledge timed out\n");
			goto error;
		}

		buffer[bufread] = '\0';

		if(strcmp(buffer, "ACK\n"))
		{
			fprintf(stderr, "\nsend: %s\n", buffer);
			goto error;
		}

		gettimeofday(&now, 0);

		seconds = now.tv_sec - start.tv_sec;
		useconds = now.tv_usec - start.tv_usec;
		duration = seconds + (useconds / 1000000.0);
		rate = done / 1024.0 / duration;

		fprintf(stderr, "%s %u kbytes in %d seconds, rate %u kbytes/s, %u %%    \r", trait->progress, done / 1024, (int)duration, (int)rate, (done * 100) / file_length);
	}

	fprintf(stderr, "\nfinishing\n");

	MD5_Final(md5_hash, &md5);
	md5_hash_to_string(md5_hash, sizeof(md5_string), md5_string);

	snprintf(cmdbuf, sizeof(cmdbuf), "of %s\n", md5_string);

	if(write_timeout(sock_fd, cmdbuf, strlen(cmdbuf), timeout) != (ssize_t)strlen(cmdbuf))
	{
		fprintf(stderr, "finish failed (%m)\n");
		goto error;
	}

	if((bufread = read_timeout(sock_fd, buffer, sizeof(buffer), timeout)) <= 0)
	{
		fprintf(stderr, "finish failed: %m\n");
		goto error;
	}

	buffer[bufread] = '\0';

	if(sscanf(buffer, "%16s %32s\n", cmdbuf, hexbuf) != 2)
	{
		fprintf(stderr, "finish failed: %s\n", buffer);
		goto error;
	}

	if(strcmp(trait->reply_ok, cmdbuf))
	{
		fprintf(stderr, "finish failed: %s\n", buffer);
		goto error;
	}

	if(strcmp(md5_string, hexbuf))
	{
		fprintf(stderr, "md5sum mismatch: \"%s\" != \"%s\"\n", md5_string, hexbuf);
		goto error;
	}

	fprintf(stderr, "%s successful\n", trait->id);

	if(!dontcommit && !verify)
	{
		snprintf(cmdbuf, sizeof(cmdbuf), "oc\n");

		if(write_timeout(sock_fd, cmdbuf, strlen(cmdbuf), timeout) != (ssize_t)strlen(cmdbuf))
		{
			fprintf(stderr, "commit write failed (%m)\n");
			goto error;
		}

		if((bufread = read_timeout(sock_fd, buffer, sizeof(buffer), timeout)) <= 0)
		{
			fprintf(stderr, "commit acknowledge failed: %m\n");
			goto error;
		}

		buffer[bufread] = '\0';

		if(strcmp("> reset\n", buffer))
		{
			fprintf(stderr, "commit failed: %s\n", buffer);
			goto error;
		}

		fprintf(stderr, "commit slot %u successful\n", slot);
	}

	rv = 0;
error:
	close(sock_fd);
	close(file_fd);

	exit(rv);
}
