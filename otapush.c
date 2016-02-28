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

static void crc32_init(void);
static uint32_t crc32(int length, const char *src);
static int verbose;

static void usage(void)
{
	fprintf(stderr, "usage: otapush [options] host file\n");
	fprintf(stderr, "-c|--dont-commit      don't commit (reset and load new image)\n");
	fprintf(stderr, "-p|--port             set command port (default 24)\n");
	fprintf(stderr, "-s|--chunk-size 2^s   set command port (5 - 10, default 8 = 256 bytes)\n");
	fprintf(stderr, "-t|--timeout ms       set communication timeout (default = 30000 = 30s)\n");
	fprintf(stderr, "-V|--verify           verify (instead of write)\n");
	fprintf(stderr, "-v|--verbose          verbose\n");
}

static void do_log(const char *tag, int msglength, const char *msg)
{
	int ix;
	char byte;

	if(!verbose)
		return;

	fprintf(stderr, "* %s: ", tag);

	if(msglength > 64)
		msglength = 64;

	for(ix = 0; ix < msglength; ix++)
	{
		byte = msg[ix];

		if((byte < ' ') || (byte > '~'))
			byte = '-';

		fputc(byte, stderr);
	}

	fprintf(stderr, "\n");
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

static int do_read(int fd, char *dst, ssize_t size, int timeout)
{
	struct pollfd pfd;
	int length;

	pfd.fd		= fd;
	pfd.events	= POLLIN;

	if(poll(&pfd, 1, timeout) != 1)
		return(-1);

	length = read(fd, dst, size);

	if(length > 0)
		dst[length] = '\0';

	if((length > 0) && (dst[length - 1] == '\n'))
		dst[--length] = '\0';

	if((length > 0) && (dst[length - 1] == '\r'))
		dst[--length] = '\0';

	return(length >= 0);
}

static int do_write(int fd, char *src, ssize_t length, int timeout)
{
	struct pollfd	pfd;

	pfd.fd		= fd;
	pfd.events	= POLLOUT;

	if(poll(&pfd, 1, timeout) != 1)
		return(-1);

	src[length++] = '\r';
	src[length++] = '\n';

	return(write(fd, src, length) == length);
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
	int					file_fd, sock_fd;
	struct sockaddr_in6	saddr;
	const char			*hostname;
	const char			*filename;
	int					file_length, done;
	int					length, written, skipped;
	char				buffer[8192], cmdbuf[8192];
	ssize_t				bufread;
	MD5_CTX				md5;
	char				md5_hash[MD5_DIGEST_LENGTH];
	char				md5_string[MD5_DIGEST_LENGTH * 2 + 1];
	char				remote_md5_string[MD5_DIGEST_LENGTH * 2 + 1];
	char				remote_remote_md5_string[MD5_DIGEST_LENGTH * 2 + 1];
	uint32_t			crc;
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
	verbose = 0;

	static const char *shortopts = "cp:s:t:vV";
	static const struct option longopts[] =
	{
		{ "dont-commmit",	no_argument,		0, 'c' },
		{ "port",			required_argument,	0, 'p' },
		{ "chunk-size",		required_argument,	0, 's' },
		{ "timeout",		required_argument,	0, 't' },
		{ "verify",			no_argument,		0, 'V' },
		{ "verbose",		no_argument,		0, 'v' },
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

			case('V'):
			{
				verify = 1;
				break;
			}

			case('v'):
			{
				verbose = 1;
				break;
			}
		}
	}

	if((argc - optind) < 2)
	{
		usage();
		exit(1);
	}

	if((chunk_size < 5) || (chunk_size > 10))
	{
		fprintf(stderr, "chunk size must be between 5 (32 bytes) and 10 (1024 bytes)\n");
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

#if 0
	val = 1;

	if(setsockopt(sock_fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val)))
	{
		fprintf(stderr, "socket set nodelay failed: %m\n");
		goto error;
	}
#endif

#if 0
	val = 1200;

	if(setsockopt(sock_fd, IPPROTO_TCP, TCP_WINDOW_CLAMP, &val, sizeof(val)))
	{
		fprintf(stderr, "socket set window clamp failed: %m\n");
		goto error;
	}
#endif

	if(connect(sock_fd, (const struct sockaddr *)&saddr, sizeof(saddr)))
	{
		fprintf(stderr, "connect failed: %m\n");
		goto error;
	}

	do_log("connect", 0, 0);

	snprintf(cmdbuf, sizeof(cmdbuf), "%s %u", trait->cmd, file_length);

	do_log("send", strlen(cmdbuf), cmdbuf);

	if(!do_write(sock_fd, cmdbuf, strlen(cmdbuf), timeout))
	{
		fprintf(stderr, "command %s failed (%m)\n", trait->id);
		goto error;
	}

	if(!do_read(sock_fd, buffer, sizeof(buffer), timeout))
	{
		fprintf(stderr, "command %s timeout: %m\n", trait->id);
		goto error;
	}

	do_log("receive", strlen(buffer), buffer);

	if(sscanf(buffer, "%16s %d %d", cmdbuf, &slot, &sector) != 3)
	{
		fprintf(stderr, "command %s failed: %s\n", trait->id, buffer);
		goto error;
	}

	if(strcmp(cmdbuf, trait->reply))
	{
		fprintf(stderr, "invalid %s response: %s\n", trait->id, buffer);
		goto error;
	}

	crc32_init();
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
		crc = crc32(bufread, buffer);

		snprintf(cmdbuf, sizeof(cmdbuf), "os %lu %u ", bufread, crc);
		length = strlen(cmdbuf);
		memcpy(cmdbuf + length, buffer, bufread);
		length += bufread;

		do_log("send data", length, cmdbuf);

		if(!do_write(sock_fd, cmdbuf, length, timeout))
		{
			fprintf(stderr, "\nsend failed (%m)\n");
			goto error;
		}

		if(!do_read(sock_fd, buffer, sizeof(buffer), timeout))
		{
			fprintf(stderr, "\nsend acknowledge timed out\n");
			goto error;
		}

		do_log("receive", strlen(buffer), buffer);

		if(strncmp(buffer, "ACK ", 4))
		{
			fprintf(stderr, "\nsend: %s\n", buffer);
			goto error;
		}

		gettimeofday(&now, 0);

		seconds = now.tv_sec - start.tv_sec;
		useconds = now.tv_usec - start.tv_usec;
		duration = seconds + (useconds / 1000000.0);
		rate = done / 1024.0 / duration;

		if(!verbose)
			fprintf(stderr, "%s %u kbytes in %d seconds, rate %u kbytes/s, %u %%    \r", trait->progress, done / 1024, (int)(duration + 0.5), (int)rate, (done * 100) / file_length);
	}

	if(!verbose)
		fprintf(stderr, "\nfinishing\n");

	MD5_Final(md5_hash, &md5);
	md5_hash_to_string(md5_hash, sizeof(md5_string), md5_string);

	snprintf(cmdbuf, sizeof(cmdbuf), "of %s\n", md5_string);

	do_log("send", strlen(cmdbuf), cmdbuf);

	if(!do_write(sock_fd, cmdbuf, strlen(cmdbuf), timeout))
	{
		fprintf(stderr, "finish failed (%m)\n");
		goto error;
	}

	if(!do_read(sock_fd, buffer, sizeof(buffer), timeout))
	{
		fprintf(stderr, "finish failed: %m\n");
		goto error;
	}

	do_log("receive", strlen(buffer), buffer);

	if(sscanf(buffer, "%s %s %s %d %d\n", cmdbuf, remote_md5_string, remote_remote_md5_string, &written, &skipped) != 5)
	{
		fprintf(stderr, "finish failed 0: %s\n", buffer);
		goto error;
	}

	if(strncmp(trait->reply_ok, cmdbuf, strlen(trait->reply_ok)))
	{
		fprintf(stderr, "finish failed 1: %s\n", buffer);
		goto error;
	}

	if(strcmp(md5_string, remote_md5_string))
	{
		fprintf(stderr, "md5sums don't match: \"%s\" != \"%s\"\n", md5_string, remote_md5_string);
		goto error;
	}

	fprintf(stderr, "%s successful, %d sectors written, %d sectors skipped\n", trait->id, written, skipped);

	if(!dontcommit && !verify)
	{
		snprintf(cmdbuf, sizeof(cmdbuf), "oc");

		do_log("send", strlen(cmdbuf), cmdbuf);

		if(!do_write(sock_fd, cmdbuf, strlen(cmdbuf), timeout))
		{
			fprintf(stderr, "commit write failed (%m)\n");
			goto error;
		}

		if(!do_read(sock_fd, buffer, strlen(buffer), timeout))
		{
			fprintf(stderr, "commit write failed (no reply)\n");
			goto error;
		}

		if(sscanf(buffer, "OTA commit slot %d\n", &written) != 1)
		{
			fprintf(stderr, "commit write failed (invalid reply)\n");
			goto error;
		}

		if(written != slot)
		{
			fprintf(stderr, "commit write failed (invalid slot)\n");
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

/**********************************************************************
 * Copyright (c) 2000 by Michael Barr.  This software is placed into
 * the public domain and may be used for any purpose.  However, this
 * notice must not be changed or removed and no warranty is either
 * expressed or implied by its publication or distribution.
 **********************************************************************/

static uint32_t string_crc_table[256];

static void crc32_init(void)
{
	unsigned int dividend, bit;
	uint32_t remainder;

	for(dividend = 0; dividend < (sizeof(string_crc_table) / sizeof(*string_crc_table)); dividend++)
	{
		remainder = dividend << (32 - 8);

		for (bit = 8; bit > 0; --bit)
		{
			if (remainder & (1 << 31))
				remainder = (remainder << 1) ^ 0x04c11db7;
			else
				remainder = (remainder << 1);
		}

		string_crc_table[dividend] = remainder;
	}
}

static uint32_t crc32(int length, const char *src)
{
	uint32_t remainder = 0xffffffff;
	uint8_t data;
	int offset;

	for(offset = 0; offset < length; offset++)
	{
		data = src[offset] ^ (remainder >> (32 - 8));
		remainder = string_crc_table[data] ^ (remainder << 8);
	}

	return(remainder ^ 0xffffffff);
}
