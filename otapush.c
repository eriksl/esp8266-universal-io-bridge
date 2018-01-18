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

enum
{
	max_attempts = 8
};

static void crc32_init(void);
static uint32_t crc32(int length, const char *src);
static unsigned int verbose, dummy, timeout, dontcommit, chunk_size, udp;

static void usage(void)
{
	fprintf(stderr, "usage: otapush [options] <action> [<args>]\n");
	fprintf(stderr, "action:\n");
	fprintf(stderr, "	read  <host> <file> <address> [<length> (default = 0x1000, one sector)] \n");
	fprintf(stderr, "	write <host> <file> [<address> (do ota partial write when specifief, otherwise do ota upgrade)]\n");
	fprintf(stderr, "-c|--dont-commit      don't commit (reset and load new image)\n");
	fprintf(stderr, "-d|--dummy            dummy write (don't commit)\n");
	fprintf(stderr, "-p|--port             set command port (default 24)\n");
	fprintf(stderr, "-s|--chunk-size       set chunk size (256 / 512 or 1024 bytes, default is 1024 bytes)\n");
	fprintf(stderr, "-t|--timeout ms       set communication timeout (default = 30000 = 30s)\n");
	fprintf(stderr, "-u|--udp              use udp instead of tcp\n");
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
	hints.ai_socktype	=	udp ? SOCK_DGRAM : SOCK_STREAM;
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

static int do_read(int fd, char *dst, ssize_t size)
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

static int do_write(int fd, char *src, ssize_t length)
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

static int do_action_read(int socket_fd, const char *filename, unsigned int address, unsigned int file_length)
{
	int				file_fd;
	char			buffer[8192], cmdbuf[8192];
	char			*data;
	unsigned int	data_offset;
	ssize_t			data_length;
	MD5_CTX			md5;
	char			md5_hash[MD5_DIGEST_LENGTH];
	char			md5_string[MD5_DIGEST_LENGTH * 2 + 1];
	char			remote_md5_string[MD5_DIGEST_LENGTH * 2 + 1];
	struct timeval	start, now;
	int				seconds, useconds;
	double			duration, rate;
	uint32_t		crc;
	unsigned int	attempt, data_received, data_transferred;
	unsigned int	remote_chunk_size, remote_data_transferred, remote_crc;

	struct pollfd	pfd;

	if((file_fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0777)) < 0)
	{
		fprintf(stderr, "cannot open file %s: %m\n", filename);
		return(1);
	}

	snprintf(cmdbuf, sizeof(cmdbuf), "ota-read\n");

	do_log("send", strlen(cmdbuf), cmdbuf);

	if(!do_write(socket_fd, cmdbuf, strlen(cmdbuf)))
	{
		fprintf(stderr, "command ota-read failed (%m)\n");
		goto error;
	}

	if(!do_read(socket_fd, buffer, sizeof(buffer)))
	{
		fprintf(stderr, "command ota-read timeout: %m\n");
		goto error;
	}

	do_log("receive", strlen(buffer), buffer);

	if(strcmp(buffer, "READ"))
	{
		fprintf(stderr, "command ota-read failed: %s\n", buffer);
		goto error;
	}

	MD5_Init(&md5);

	fprintf(stderr, "start ota read from address: 0x%06x, length: %u, to file: %s\n", address, file_length, filename);

	data_transferred = 0;
	data_received = 0;
	gettimeofday(&start, 0);

	for(data_received = 0; data_received < file_length;)
	{
		for(attempt = 0; attempt < max_attempts; attempt++)
		{
			if(attempt > 0)
				fprintf(stderr, "retry, attempt: %d\n", attempt);

			if((data_received + chunk_size) > file_length)
				data_length = file_length - data_received;
			else
				data_length = chunk_size;

			snprintf(cmdbuf, sizeof(cmdbuf), "ota-receive-data %u %u\n", address, (unsigned int)data_length);

			if(!do_write(socket_fd, cmdbuf, strlen(cmdbuf)))
			{
				fprintf(stderr, "\nsend request data failed (%m)\n");
				continue;
			}

			pfd.fd		= socket_fd;
			pfd.events	= POLLIN;

			if(poll(&pfd, 1, timeout) != 1)
			{
				fprintf(stderr, "\nreceive data timed out\n");
				return(-1);
			}

			if((data_length = read(socket_fd, buffer, sizeof(buffer))) <= 0)
			{
				fprintf(stderr, "\nreceive data error (%m)\n");
				continue;
			}

			buffer[data_length] = '\0';

			do_log("receive data", data_length, buffer);

			if(sscanf(buffer, "DATA %u %u %u @%n", &remote_chunk_size, &remote_data_transferred, &remote_crc, &data_offset) != 3)
			{
				fprintf(stderr, "\nreceive data failed, no DATA received\n");
				continue;
			}

			data = &buffer[data_offset];
			data_length -= data_offset;
			data_transferred += data_length;

			if(remote_chunk_size != data_length)
			{
				fprintf(stderr, "\nreceived data failed: remote chunk length(%u) != data length(%u)\n", remote_chunk_size, (unsigned int)data_length);
				fprintf(stderr, "offset: %u\n", data_offset);
				continue;
			}

			if(remote_data_transferred != data_transferred)
			{
				fprintf(stderr, "invalid ota-read response: data transferred differ, remote:%u, local:%u\n", remote_data_transferred, data_transferred);
				goto error;
			}

			crc = crc32(data_length, data);

			if(crc != remote_crc)
			{
				fprintf(stderr, "\nreceived data failed: crc mismatch, local:%u, remote:%u\n", crc, remote_crc);
				continue;
			}

			break;
		}

		if(attempt >= max_attempts)
		{
			fprintf(stderr, "\nmax tries to receive chunk failed\n");
			goto error;
		}

		MD5_Update(&md5, data, data_length);
		data_received += data_length;
		address += data_length;

		if(write(file_fd, data, data_length) != data_length)
		{
			fprintf(stderr, "\nfile write error: %m\n");
			goto error;
		}

		gettimeofday(&now, 0);
		seconds = now.tv_sec - start.tv_sec;
		useconds = now.tv_usec - start.tv_usec;
		duration = seconds + (useconds / 1000000.0);
		rate = data_transferred / 1024.0 / duration;

		if(!verbose)
			fprintf(stderr, "received %u kbytes in %d seconds, rate %u kbytes/s, %u %%    \r",
					data_transferred / 1024, (int)(duration + 0.5), (int)rate, (data_received * 100) / file_length);
	}

	if(!verbose)
		fprintf(stderr, "\nfinishing\n");

	close(file_fd);

	snprintf(cmdbuf, sizeof(cmdbuf), "ota-finish\n");

	do_log("send", strlen(cmdbuf), cmdbuf);

	if(!do_write(socket_fd, cmdbuf, strlen(cmdbuf)))
	{
		fprintf(stderr, "finish failed (%m)\n");
		return(1);
	}

	if(!do_read(socket_fd, buffer, sizeof(buffer)))
	{
		fprintf(stderr, "finish failed: %m\n");
		return(1);
	}

	do_log("receive", strlen(buffer), buffer);

	if(sscanf(buffer, "READ_OK %s %u\n", remote_md5_string, &remote_data_transferred) != 2)
	{
		fprintf(stderr, "finish failed: %s\n", buffer);
		return(1);
	}

	if(data_transferred != remote_data_transferred)
	{
		fprintf(stderr, "finish failed: data transferred != remote data transferred\n");
		return(1);
	}

	MD5_Final(md5_hash, &md5);
	md5_hash_to_string(md5_hash, sizeof(md5_string), md5_string);

	if(strcmp(md5_string, remote_md5_string))
	{
		fprintf(stderr, "finish failed: md5sums don't match: \"%s\" != \"%s\"\n", md5_string, remote_md5_string);
		return(1);
	}

	fprintf(stderr, "receive successful, %u sectors received, %u dups\n", data_transferred / 0x1000, (data_transferred - data_received) / 0x1000);

	return(0);

error:
	close(file_fd);
	return(1);
}

static int do_action_write(int socket_fd, const char *filename, int address)
{
	int				file_fd, file_length;
	char			readbuffer[8192], buffer[8192], cmdbuf[8192];
	ssize_t			bufread;
	MD5_CTX			md5;
	char			md5_hash[MD5_DIGEST_LENGTH];
	char			md5_string[MD5_DIGEST_LENGTH * 2 + 1];
	char			remote_md5_string[MD5_DIGEST_LENGTH * 2 + 1];
	char			remote_remote_md5_string[MD5_DIGEST_LENGTH * 2 + 1];
	struct timeval	start, now;
	int				seconds, useconds;
	double			duration, rate;
	int				slot, sector;
	uint32_t		crc;
	int				done, length, written, skipped, attempt, compatibility = 0;

	if((file_fd = open(filename, O_RDONLY, 0)) < 0)
	{
		fprintf(stderr, "cannot open file %s: %m\n", filename);
		return(1);
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

	if(address >= 0)
		snprintf(cmdbuf, sizeof(cmdbuf), "ota-write%s %u %u", dummy ? "-dummy" : "",
				file_length, address);
	else
		snprintf(cmdbuf, sizeof(cmdbuf), "ota-write%s %u", dummy ? "-dummy" : "",
				file_length); 

	do_log("send", strlen(cmdbuf), cmdbuf);

	if(!do_write(socket_fd, cmdbuf, strlen(cmdbuf)))
	{
		fprintf(stderr, "command ota-write failed (%m)\n");
		goto error;
	}

	if(!do_read(socket_fd, buffer, sizeof(buffer)))
	{
		fprintf(stderr, "command ota-write timeout: %m\n");
		goto error;
	}

	do_log("receive", strlen(buffer), buffer);

	if(sscanf(buffer, "%16s %d %d", cmdbuf, &slot, &sector) != 3)
	{
		fprintf(stderr, "command ota-write failed: %s\n", buffer);
		goto error;
	}

	if(strcmp(cmdbuf, "WRITE"))
	{
		fprintf(stderr, "invalid ota-write response: %s\n", buffer);
		goto error;
	}

	MD5_Init(&md5);

	if(address >= 0)
		fprintf(stderr, "start send of file: %s, length: %u to address: 0x%06x\n", filename, file_length, sector * 0x1000);
	else
		fprintf(stderr, "start ota upgrade with file: %s, length: %u to slot: %d at address: 0x%06x\n", filename, file_length, slot, sector * 0x1000);

	gettimeofday(&start, 0);

	for(done = 0;;)
	{
		if((bufread = read(file_fd, readbuffer, chunk_size)) < 0)
		{
			fprintf(stderr, "\nfile read failed: %m\n");
			goto error;
		}

		readbuffer[bufread] = '\0';

		if(bufread == 0)
			break;

		done += bufread;

		MD5_Update(&md5, readbuffer, bufread);
		crc = crc32(bufread, readbuffer);

		for(attempt = 0; attempt < max_attempts; attempt++)
		{
			if(attempt > 0)
				fprintf(stderr, "retry, attempt: %d\n", attempt);

			if(!compatibility)
				snprintf(cmdbuf, sizeof(cmdbuf), "ota-send-data %lu %u ", bufread, crc);
			else
				snprintf(cmdbuf, sizeof(cmdbuf), "os %lu %u ", bufread, crc);

			length = strlen(cmdbuf);
			memcpy(cmdbuf + length, readbuffer, bufread);
			length += bufread;

			do_log("send data", length, cmdbuf);

			if(!do_write(socket_fd, cmdbuf, length))
			{
				fprintf(stderr, "\nsend failed (%m)\n");
				continue;
			}

			if(!do_read(socket_fd, buffer, sizeof(buffer)))
			{
				fprintf(stderr, "\nsend acknowledge timed out\n");
				continue;
			}

			do_log("receive", strlen(buffer), buffer);

			if(strncmp(buffer, "ACK ", 4))
			{
				fprintf(stderr, "\nsend: %s\n", buffer);

				if(compatibility)
				{
					fprintf(stderr, "switching to normal mode\n");
					compatibility = 0;
				}
				else
				{
					fprintf(stderr, "switching to compatibility mode\n");
					compatibility = 1;
				}

				continue;
			}

			break;
		}

		if(attempt >= max_attempts)
		{
			fprintf(stderr, "\nmax tries to send failed\n");
			goto error;
		}

		gettimeofday(&now, 0);

		seconds = now.tv_sec - start.tv_sec;
		useconds = now.tv_usec - start.tv_usec;
		duration = seconds + (useconds / 1000000.0);
		rate = done / 1024.0 / duration;

		if(!verbose)
			fprintf(stderr, "sent %u kbytes in %d seconds, rate %u kbytes/s, %u %%    \r",
					done / 1024, (int)(duration + 0.5), (int)rate, (done * 100) / file_length);
	}

	if(!verbose)
		fprintf(stderr, "\nfinishing\n");

	close(file_fd);

	MD5_Final(md5_hash, &md5);
	md5_hash_to_string(md5_hash, sizeof(md5_string), md5_string);

	snprintf(cmdbuf, sizeof(cmdbuf), "ota-finish %s\n", md5_string);

	do_log("send", strlen(cmdbuf), cmdbuf);

	if(!do_write(socket_fd, cmdbuf, strlen(cmdbuf)))
	{
		fprintf(stderr, "finish failed (%m)\n");
		return(1);
	}

	if(!do_read(socket_fd, buffer, sizeof(buffer)))
	{
		fprintf(stderr, "finish failed: %m\n");
		return(1);
	}

	do_log("receive", strlen(buffer), buffer);

	if(sscanf(buffer, "%s %s %s %d %d\n", cmdbuf, remote_md5_string, remote_remote_md5_string, &written, &skipped) != 5)
	{
		fprintf(stderr, "finish failed 0: %s\n", buffer);
		return(1);
	}

	if(dummy)
	{
		if(strncmp((address < 0) ? "DUMMY_WRITE_OK" : "PARTIAL_DUMMY_WRITE_OK", cmdbuf, 8))
		{
			fprintf(stderr, "finish failed 1: %s\n", buffer);
			return(1);
		}
	}
	else
	{
		if(strncmp((address < 0) ? "WRITE_OK" : "PARTIAL_WRITE_OK", cmdbuf, 8))
		{
			fprintf(stderr, "finish failed 1: %s\n", buffer);
			return(1);
		}
	}

	if(strcmp(md5_string, remote_md5_string))
	{
		fprintf(stderr, "md5sums don't match: \"%s\" != \"%s\"\n", md5_string, remote_md5_string);
		return(1);
	}

	fprintf(stderr, "%s successful, %d sectors written, %d sectors skipped\n", (address < 0) ? "upgrade" : "write to flash", written, skipped);

	if((address >= 0) || dontcommit || dummy)
		return(0);

	snprintf(cmdbuf, sizeof(cmdbuf), "ota-commit");

	do_log("send", strlen(cmdbuf), cmdbuf);

	if(!do_write(socket_fd, cmdbuf, strlen(cmdbuf)))
	{
		fprintf(stderr, "commit write failed (%m)\n");
		return(1);
	}

	if(!do_read(socket_fd, buffer, strlen(buffer)))
	{
		fprintf(stderr, "commit write failed (no reply)\n");
		return(1);
	}

	if(sscanf(buffer, "OTA commit slot %d\n", &written) != 1)
	{
		fprintf(stderr, "commit write failed (invalid reply)\n");
		return(1);
	}

	if(written != slot)
	{
		fprintf(stderr, "commit write failed (invalid slot)\n");
		return(1);
	}

	fprintf(stderr, "commit slot %u successful\n", slot);

	return(0);

error:
	close(file_fd);
	return(1);
}

typedef enum
{
	action_read,
	action_write
} action_t;

int main(int argc, char * const *argv)
{
	static const char *shortopts = "cdp:s:t:uv";
	static const struct option longopts[] =
	{
		{ "dont-commmit",	no_argument,		0, 'c' },
		{ "dummy",			no_argument,		0, 'd' },
		{ "port",			required_argument,	0, 'p' },
		{ "chunk-size",		required_argument,	0, 's' },
		{ "timeout",		required_argument,	0, 't' },
		{ "udp",			no_argument,		0, 'u' },
		{ "verbose",		no_argument,		0, 'v' },
		{ 0, 0, 0, 0 }
	};

	int					socket_fd = -1;
	struct sockaddr_in6	saddr;
	const char			*actionstr, *hostname, *filename;
	int					address, length;
	action_t			action;
	int					arg;
	int					port = 24;
	int					rv = -1;

	dontcommit = 0;
	chunk_size = 1024;
	timeout = 30000;
	verbose = 0;
	udp = 0;

	while((arg = getopt_long(argc, argv, shortopts, longopts, 0)) != -1)
	{
		switch(arg)
		{
			case('c'):
			{
				dontcommit = 1;
				break;
			}

			case('d'):
			{
				dummy = 1;
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

			case('u'):
			{
				udp = 1;
				break;
			}

			case('v'):
			{
				verbose = 1;
				break;
			}
		}
	}

	if((chunk_size != 256) && (chunk_size != 512) && (chunk_size != 1024))
	{
		fprintf(stderr, "chunk size must be either 256, 512 or 1024 bytes)\n");
		exit(1);
	}

	if((argc - optind) < 3)
	{
		usage();
		exit(1);
	}

	crc32_init();

	actionstr	= argv[optind + 0];
	hostname	= argv[optind + 1];
	filename	= argv[optind + 2];

	if(!strcmp(actionstr, "read"))
		action = action_read;
	else
		if(!strcmp(actionstr, "write"))
			action = action_write;
		else
		{
			usage();
			exit(-1);
		}

	if(!resolve(hostname, port, &saddr))
	{
		fprintf(stderr, "cannot resolve hostname %s: %m\n", hostname);
		return(-1);
	}

	if((socket_fd = socket(AF_INET6, udp ? SOCK_DGRAM : SOCK_STREAM, 0)) < 0)
	{
		fprintf(stderr, "socket failed: %m\n");
		goto error;
	}

	if(connect(socket_fd, (const struct sockaddr *)&saddr, sizeof(saddr)))
	{
		fprintf(stderr, "connect failed: %m\n");
		goto error;
	}

	do_log("connect", 0, 0);

	switch(action)
	{
		case(action_read):
		{
			if((argc - optind) < 4)
			{
				usage();
				exit(1);
			}

			address = strtoul(argv[optind + 3], 0, 0);

			if((argc - optind) < 5)
				length = 0x1000;
			else
				length = strtoul(argv[optind + 4], 0, 0);

fprintf(stderr, "---- length: %u\n", length);

			rv = do_action_read(socket_fd, filename, address, length);

			break;
		}

		case(action_write):
		{
			if((argc - optind) < 4)
				address = -1;
			else
				address = strtoul(argv[optind + 3], 0, 0);

			if(address == 0)
			{
				fprintf(stderr, "writing to address 0x00000, that's probably not what you want, aborting\n");
				goto error;
			}

			rv = do_action_write(socket_fd, filename, address);
		}
	}

error:
	close(socket_fd);

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
