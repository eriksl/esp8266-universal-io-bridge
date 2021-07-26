#include <string>
#include <vector>
#include <ios>
#include <iomanip>
#include <iostream>

#include <boost/regex.hpp>
#include <boost/program_options.hpp>
namespace po = boost::program_options;

#include <netinet/in.h>
#include <netdb.h>
#include <sys/poll.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <netinet/tcp.h>

#include <openssl/sha.h>

enum
{
	max_attempts = 4,
	max_udp_packet_size = 1472,
	flash_sector_size = 4096,
};

typedef std::vector<std::string> StringVector;

class GenericSocket
{
	private:

		int fd;
		std::string host;
		std::string service;
		bool use_udp, verbose;

	public:
		GenericSocket(const std::string &host, const std::string &port, bool use_udp, bool verbose);
		~GenericSocket();

		bool send(int timeout_msec, std::string buffer, bool raw = false);
		bool receive(int timeout_msec, std::string &buffer, int expected, bool raw);
		void reconnect();
};

void GenericSocket::reconnect()
{
    struct addrinfo hints;
    struct addrinfo *res;
	struct sockaddr_in6 saddr;
	//int val;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET6;
    hints.ai_socktype = use_udp ? SOCK_DGRAM : SOCK_STREAM;
    hints.ai_flags = AI_NUMERICSERV | AI_V4MAPPED;

    if(getaddrinfo(host.c_str(), service.c_str(), &hints, &res))
    {
        freeaddrinfo(res);
		throw(std::string("unknown host"));
    }

    saddr = *(struct sockaddr_in6 *)res->ai_addr;
    freeaddrinfo(res);

	if(fd >= 0)
		close(fd);

	if((fd = socket(AF_INET6, use_udp ? SOCK_DGRAM : SOCK_STREAM, 0)) < 0)
		throw(std::string("socket failed"));

	if(connect(fd, (const struct sockaddr *)&saddr, sizeof(saddr)))
		throw(std::string("connect failed"));
}

GenericSocket::GenericSocket(const std::string &host_in, const std::string &service_in, bool use_udp_in, bool verbose_in)
		: host(host_in), service(service_in), use_udp(use_udp_in), verbose(verbose_in)
{
	fd = -1;

	this->reconnect();
}

GenericSocket::~GenericSocket()
{
	if(fd >= 0)
		close(fd);
}

bool GenericSocket::send(int timeout, std::string buffer, bool raw)
{
	struct pollfd pfd;
	ssize_t chunk;

	if(raw)
	{
		if(buffer.length() == 0)
		{
			write(fd, buffer.data(), 0);
			return(true);
		}
	}
	else
		buffer += "\r\n";

	while(buffer.length() > 0)
	{
		pfd.fd = fd;
		pfd.events = POLLOUT | POLLERR | POLLHUP;
		pfd.revents = 0;

		if(poll(&pfd, 1, timeout) != 1)
			return(false);

		if(pfd.revents & (POLLERR | POLLHUP))
			return(false);

		chunk = (ssize_t)buffer.length();

		if(use_udp && (chunk > max_udp_packet_size))
			chunk = max_udp_packet_size;

		if(write(fd, buffer.data(), chunk) != chunk)
			return(false);

		buffer.erase(0, chunk);
	}

	return(true);
}

bool GenericSocket::receive(int timeout, std::string &reply, int expected, bool raw)
{
	int length;
	int run;
	struct pollfd pfd = { .fd = fd, .events = POLLIN | POLLERR | POLLHUP, .revents = 0 };
	char buffer[flash_sector_size];

	reply.clear();

	for(run = 8; run > 0; run--)
	{
		if(poll(&pfd, 1, timeout) != 1)
			return(false);

		if(pfd.revents & (POLLERR | POLLHUP))
			return(false);

		if((length = read(fd, buffer, sizeof(buffer) - 1)) < 0)
			return(false);

		if(length == 0)
			break;

		reply.append(buffer, (size_t)length);

		if((expected > 0) && (expected > length))
			expected -= length;
		else
			break;
	}

	if(!raw && (reply.back() == '\n'))
		reply.pop_back();

	if(!raw && (reply.back() == '\r'))
		reply.pop_back();

	return(true);
}

static std::string sha_hash_to_text(const unsigned char *hash)
{
	unsigned int current;
	std::stringstream hash_string;

	for(current = 0; current < SHA_DIGEST_LENGTH; current++)
		hash_string << std::hex << std::setw(2) << std::setfill('0') << (unsigned int)hash[current];

	return(hash_string.str());
}

static void process(GenericSocket &channel, const std::string &send_string, std::string &reply_string, const std::string &match,
		std::vector<std::string> &string_value, std::vector<int> &int_value, bool verbose,
		int timeout = 2000, unsigned int attempts = 4)
{
	boost::regex re(match);
	boost::smatch capture;
	unsigned int captures;
	unsigned int attempt;

	reply_string.clear();

	if(verbose)
	{
		int length = 0;

		std::cout << "> send (" << send_string.length() << "): ";

		for(const auto &it : send_string)
		{
			if(length++ > 80)
				break;

			if((it < ' ') || (it > '~'))
				std::cout << '.';
			else
				std::cout << it;
		}

		std::cout << std::endl;
	}

	for(attempt = attempts; attempt > 0; attempt--)
	{
		bool send_status, receive_status;

		send_status = channel.send(timeout, send_string);

		if(send_status)
			receive_status = channel.receive(timeout, reply_string, -1, false);
		else
			receive_status = false;

		if(reply_string.length() == 0)
			receive_status = false;

		if(send_status && receive_status)
			break;

		std::cout << std::endl << (!send_status ? "send" : "receive") << " failed, retry #" << (attempts - attempt) << std::endl;

		channel.reconnect();
	}

	if(verbose)
		std::cout << "< receive: " << reply_string << std::endl;

	if(!boost::regex_match(reply_string, capture, re))
		throw(std::string("received string does not match: \"") + reply_string + "\"");

	string_value.clear();
	int_value.clear();
	captures = 0;

	for(const auto &it : capture)
	{
		if(captures++ == 0)
			continue;

		string_value.push_back(std::string(it));

		try
		{
			int_value.push_back(stoi(it, 0, 0));
		}
		catch(...)
		{
			int_value.push_back(0);
		}
	}
}

void command_write(GenericSocket &command_channel, GenericSocket &mailbox_channel, const std::string filename, unsigned int start,
	int chunk_size, bool verbose, bool simulate, bool otawrite)
{
	int fd;
	int64_t file_offset;
	struct stat stat;
	unsigned char sector_buffer[flash_sector_size];
	unsigned char sector_hash[SHA_DIGEST_LENGTH];
	unsigned char file_hash[SHA_DIGEST_LENGTH];
	unsigned int sector_length, sector_attempt;
	unsigned int current, length;
	int sectors_written, sectors_skipped, sectors_erased;
	struct timeval time_start, time_now;
	std::string sha_local_hash_text;
	std::string sha_remote_hash_text;
	std::string send_string;
	std::string reply;
	std::string operation;
	std::vector<int> int_value;
	std::vector<std::string> string_value;
	SHA_CTX sha_file_ctx;

	sectors_skipped = 0;
	sectors_erased = 0;
	sectors_written = 0;

	if(filename.empty())
		throw(std::string("file name required"));

	if((fd = open(filename.c_str(), O_RDONLY, 0)) < 0)
		throw(std::string("file not found"));

	fstat(fd, &stat);
	file_offset = stat.st_size;
	length = (file_offset + (flash_sector_size - 1)) / flash_sector_size;

	try
	{
		gettimeofday(&time_start, 0);

		std::cout << "start ";

		if(simulate)
			std::cout << "simulate";
		else
		{
			if(otawrite)
				std::cout << "ota ";
			else
				std::cout << "normal ";

			std::cout << "write";
		}

		std::cout << " at address: 0x" << std::hex << std::setw(6) << std::setfill('0') << (start * flash_sector_size) << " (sector " << std::dec << start << ")";
		std::cout << ", length: " << std::dec << std::setw(0) << (length * flash_sector_size) << " (" << length << " sectors)";
		std::cout << ", chunk size: " << chunk_size << std::endl;

		SHA1_Init(&sha_file_ctx);

		process(command_channel, "mailbox-reset", reply, "OK mailbox-reset", string_value, int_value, verbose);

		for(current = start; current < (start + length); current++)
		{
			memset(sector_buffer, 0xff, flash_sector_size);

			if((sector_length = read(fd, sector_buffer, flash_sector_size)) <= 0)
				throw(std::string("i/o error in read"));

			if((file_offset = lseek(fd, 0, SEEK_CUR)) < 0)
				throw(std::string("i/o error in seek"));

			SHA1(sector_buffer, flash_sector_size, sector_hash);
			sha_local_hash_text = sha_hash_to_text(sector_hash);

			SHA1_Update(&sha_file_ctx, sector_buffer, flash_sector_size);

			for(sector_attempt = max_attempts; sector_attempt > 0; sector_attempt--)
			{
				try
				{
					if(verbose)
					{
						std::cout << "sending sector: " << current << ", #" << (current - start);
						std::cout << ", offset: " << file_offset << ", try #" << (max_attempts - sector_attempt) << std::endl;
					}

					if(!mailbox_channel.send(100, std::string((const char *)sector_buffer, flash_sector_size), true /* raw */))
						throw(std::string("send failed"));

					if(!mailbox_channel.receive(100, reply, -1, true))
						throw(std::string("receive failed"));

					if(verbose)
						std::cout << "mailbox replied: \"" << reply << "\"" << std::endl;

					if(simulate)
						process(command_channel, std::string("mailbox-simulate ") + std::to_string(current), reply,
								"OK mailbox-simulate: received sector ([0-9]+), erased: ([0-9]), skipped ([0-9]), checksum: ([0-9a-f]+)",
								string_value, int_value, verbose);
					else
						process(command_channel, std::string("mailbox-write ") + std::to_string(current), reply,
								"OK mailbox-write: written sector ([0-9]+), erased: ([0-9]), skipped ([0-9]), checksum: ([0-9a-f]+)",
								string_value, int_value, verbose);

					if(int_value[0] != (int)current)
						throw(std::string("local sector (") + std::to_string(current) + ") != remote sector (" + std::to_string(int_value[0]) + ")");

					if(int_value[1] == 0)
						sectors_written++;
					else
						sectors_skipped++;

					if(int_value[2] != 0)
						sectors_erased++;

					sha_remote_hash_text = string_value[3];

					if(sha_local_hash_text != sha_remote_hash_text)
						throw(std::string("local hash (") + sha_local_hash_text + ") != remote hash (" + sha_remote_hash_text + ")");

					if(verbose)
					{
						std::cout << "sector verified";
						std::cout << ", local hash: " << sha_local_hash_text;
						std::cout << ", remote hash: " << sha_remote_hash_text << std::endl;
					}

					break;
				}
				catch(const std::string &e)
				{
					if(!verbose)
						std::cout << std::endl;

					std::cout << "! send sector failed: " << e;
					std::cout << ", sector #" << (current - start) << "/" << length;
					std::cout << std::endl;

					usleep(100000);
					mailbox_channel.reconnect();
					process(command_channel, "mailbox-reset", reply, "OK mailbox-reset", string_value, int_value, verbose);
				}
			}

			if(sector_attempt == 0)
				throw(std::string("sending sector failed too many times"));

			if(verbose)
			{
				std::cout << "sector written";
				std::cout << ", local hash: " << sha_local_hash_text;
				std::cout << ", remote hash: " << sha_remote_hash_text;
				std::cout << ", try #" << (max_attempts - sector_attempt) << std::endl;
			}
			else
			{
				int seconds, useconds;
				double duration, rate;

				gettimeofday(&time_now, 0);

				seconds = time_now.tv_sec - time_start.tv_sec;
				useconds = time_now.tv_usec - time_start.tv_usec;
				duration = seconds + (useconds / 1000000.0);
				rate = file_offset / 1024.0 / duration;

				std::cout << std::setfill(' ');
				std::cout << "sent "		<< std::setw(4) << (file_offset / 1024) << " kbytes";
				std::cout << " in "			<< std::setw(4) << std::setprecision(2) << std::fixed << duration << " seconds";
				std::cout << " at rate "	<< std::setw(4) << std::setprecision(0) << std::fixed << rate << " kbytes/s";
				std::cout << ", sent "		<< std::setw(3) << (current - start + 1) << " sectors";
				std::cout << ", written "	<< std::setw(3) << sectors_written << " sectors";
				std::cout << ", erased "	<< std::setw(3) << sectors_erased << " sectors";
				std::cout << ", skipped "	<< std::setw(3) << sectors_skipped << " sectors";
				std::cout << ", "			<< std::setw(3) << (((file_offset + flash_sector_size) * 100) / (length * flash_sector_size)) << "%       \r";
				std::cout.flush();
			}
		}
	}
	catch(std::string &e)
	{
		close(fd);
		throw;
	}

	close(fd);

	if(!verbose)
		std::cout << std::endl;

	if(!simulate)
	{
		std::cout << "checksumming " << length << " sectors..." << std::endl;

		SHA1_Final(file_hash, &sha_file_ctx);
		sha_local_hash_text = sha_hash_to_text(file_hash);

		send_string = std::string("mailbox-checksum ") + std::to_string(start) + " " + std::to_string(length);
		process(command_channel, send_string, reply, "OK mailbox-checksum: checksummed sectors: ([0-9]+), from sector: ([0-9]+), checksum: ([0-9a-f]+)",
				string_value, int_value, verbose);

		if(verbose)
		{
			std::cout << "local checksum:  " << sha_local_hash_text << std::endl;
			std::cout << "remote checksum: " << string_value[2] << std::endl;
		}

		if(int_value[0] != (int)length)
			throw(std::string("checksum failed: length differs, local: ") + std::to_string(length) + ", remote: " + std::to_string(int_value[0]));

		if((unsigned int)int_value[1] != start)
			throw(std::string("checksum failed: start address differs, local: ") +  std::to_string(start) + ", remote: " + std::to_string(int_value[1]));

		if(string_value[2] != sha_local_hash_text)
			throw(std::string("checksum failed: SHA hash differs, local: ") +  sha_local_hash_text + ", remote: " + string_value[2]);

		std::cout << "checksum OK" << std::endl;
		std::cout << "write finished" << std::endl;
	}
}

static void command_checksum(GenericSocket &command_channel, const std::string &filename, unsigned int start, bool verbose)
{
	int fd;
	int64_t file_length;
	struct stat stat;
	uint8_t sector_buffer[flash_sector_size];
	unsigned char file_hash[SHA_DIGEST_LENGTH];
	unsigned int current, length;
	int sector_length;
	std::string sha_local_hash_text;
	std::string sha_remote_hash_text;
	std::string send_string;
	std::string reply;
	std::string operation;
	std::vector<int> int_value;
	std::vector<std::string> string_value;
	SHA_CTX sha_file_ctx;

	if(filename.empty())
		throw(std::string("file name required"));

	if((fd = open(filename.c_str(), O_RDONLY, 0)) < 0)
		throw(std::string("file not found"));

	fstat(fd, &stat);
	file_length = stat.st_size;
	length = (file_length + (flash_sector_size - 1)) / flash_sector_size;

	try
	{
		std::cout << "start checksum from 0x" << std::hex << (start * flash_sector_size) << " (sector " << std::dec << start;
		std::cout << "), length: " << file_length << " (" << length << " sectors" << ")" << std::endl;

		SHA1_Init(&sha_file_ctx);

		for(current  = 0; current < length; current++)
		{
			memset(sector_buffer, 0xff, flash_sector_size);

			if((sector_length = read(fd, sector_buffer, flash_sector_size)) <= 0)
				throw(std::string("i/o error in read"));

			SHA1_Update(&sha_file_ctx, sector_buffer, flash_sector_size);
		}
	}
	catch(std::string &e)
	{
		close(fd);
		throw;
	}

	close(fd);

	std::cout << "checksumming " << length << " sectors..." << std::endl;

	SHA1_Final(file_hash, &sha_file_ctx);
	sha_local_hash_text = sha_hash_to_text(file_hash);

	send_string = std::string("mailbox-checksum ") + std::to_string(start) +  " " + std::to_string(length);
	process(command_channel, send_string, reply, "OK mailbox-checksum: checksummed sectors: ([0-9]+), from sector: ([0-9]+), checksum: ([0-9a-f]+)", string_value, int_value, verbose);

	if(verbose)
	{
		std::cout << "local checksum:  " << sha_local_hash_text << std::endl;
		std::cout << "remote checksum: " << string_value[2] << std::endl;
	}

	if(int_value[0] != (int)length)
		throw(std::string("checksum failed: checksummed bytes differs, local: ") + std::to_string(length) + ", remote: " + std::to_string(int_value[0]));

	if((unsigned int)int_value[1] != start)
		throw(std::string("checksum failed: start address differs, local: ") +  std::to_string(start) + ", remote: " + std::to_string(int_value[1]));

	if(string_value[2] != sha_local_hash_text)
		throw(std::string("checksum failed: SHA hash differs, local: ") + sha_local_hash_text + ", remote: " + string_value[2]);

	std::cout << "checksum OK" << std::endl;
}

static void command_read(GenericSocket &command_channel, GenericSocket &mailbox_channel, const std::string &filename, int start, int length, bool verbose)
{
	int64_t file_offset;
	int fd;
	int sector_attempt, current;
	struct timeval time_start, time_now;
	std::string send_string;
	std::string reply;
	std::string operation;
	std::vector<int> int_value;
	std::vector<std::string> string_value;
	SHA_CTX sha_file_ctx;
	unsigned char sector_hash[SHA_DIGEST_LENGTH];
	unsigned char file_hash[SHA_DIGEST_LENGTH];
	std::string sha_local_hash_text;
	std::string sha_remote_hash_text;

	if(filename.empty())
		throw(std::string("file name required"));

	if((fd = open(filename.c_str(), O_WRONLY | O_TRUNC | O_CREAT, 0666)) < 0)
		throw(std::string("can't create file"));

	try
	{
		gettimeofday(&time_start, 0);

		std::cout << "start read from 0x" << std::hex << start * flash_sector_size;
		std::cout << ", length: 0x" << std::hex << length * flash_sector_size << " (" << std::dec << length * flash_sector_size << ")";
		std::cout << std::endl;

		SHA1_Init(&sha_file_ctx);

		current = start;

		for(current = start; current < (start + length); current++)
		{
			for(sector_attempt = max_attempts; sector_attempt > 0; sector_attempt--)
			{
				send_string = std::string("mailbox-read ") + std::to_string(current);
				process(command_channel, send_string, reply, "OK mailbox-read: sending sector ([0-9]+), checksum: ([0-9a-f]+)", string_value, int_value, verbose);

				if(int_value[0] != current)
					throw(std::string("local address (") + std::to_string(current) + ") != remote address (" + std::to_string(int_value[1]) + ")");

				sha_remote_hash_text = string_value[1];

				reply.clear();

				if(mailbox_channel.receive(100, reply, flash_sector_size, true /* raw */))
					break;

				if(!verbose)
					std::cout << std::endl;

				std::cout << "! receive failed, sector #" << current << ", #" << (current - start) << ", attempt #" << max_attempts - sector_attempt;
				std::cout << std::endl;
				usleep(100000);
				mailbox_channel.reconnect();
			}

			if(sector_attempt <= 0)
				throw(std::string("! receiving sector failed too many times"));

			SHA1((const unsigned char *)reply.data(), flash_sector_size, sector_hash);
			sha_local_hash_text = sha_hash_to_text(sector_hash);

			if(verbose)
			{
				std::cout << "+ sector #" << (current - start) << ", " << current << ", address: 0x" << std::hex << (current * flash_sector_size) << std::dec << " read";
				std::cout << ", try #" << (max_attempts - sector_attempt);
				std::cout << ", local hash: " << sha_local_hash_text;
				std::cout << ", remote hash: " << sha_remote_hash_text;
				std::cout << std::endl;
			}

			if(sha_local_hash_text != sha_remote_hash_text)
			{
				std::cout << "! local hash (" << sha_local_hash_text << ") != remote hash (" << sha_remote_hash_text << ")" << std::endl;
				continue;
			}

			if((file_offset = lseek(fd, 0, SEEK_CUR)) < 0)
				throw(std::string("i/o error in seek"));

			if(write(fd, reply.data(), flash_sector_size) <= 0)
				throw(std::string("i/o error in write"));

			SHA1_Update(&sha_file_ctx, (const unsigned char *)reply.data(), flash_sector_size);

			if(!verbose)
			{
				int seconds, useconds;
				double duration, rate;

				gettimeofday(&time_now, 0);

				seconds = time_now.tv_sec - time_start.tv_sec;
				useconds = time_now.tv_usec - time_start.tv_usec;
				duration = seconds + (useconds / 1000000.0);
				rate = file_offset / 1024.0 / duration;

				std::cout << std::setfill(' ');
				std::cout << "received "	<< std::setw(3) << (file_offset / 1024) << " kbytes";
				std::cout << " in "			<< std::setw(4) << std::setprecision(2) << std::fixed << duration << " seconds";
				std::cout << " at rate "	<< std::setw(4) << std::setprecision(0) << std::fixed << rate << " kbytes/s";
				std::cout << ", received "	<< std::setw(2) << (current - start) << " sectors";
				std::cout << ", "			<< std::setw(3) << (((file_offset + flash_sector_size) * 100) / (length * flash_sector_size)) << "%       \r";
				std::cout.flush();
			}
		}
	}
	catch(const std::string &e)
	{
		close(fd);
		throw;
	}

	close(fd);

	if(!verbose)
		std::cout << std::endl;

	std::cout << "checksumming " << length << " sectors..." << std::endl;

	SHA1_Final(file_hash, &sha_file_ctx);
	sha_local_hash_text = sha_hash_to_text(file_hash);

	send_string = std::string("mailbox-checksum ") + std::to_string(start) +  " " + std::to_string(length);
	process(command_channel, send_string, reply, "OK mailbox-checksum: checksummed sectors: ([0-9]+), from sector: ([0-9]+), checksum: ([0-9a-f]+)", string_value, int_value, verbose);

	if(verbose)
	{
		std::cout << "local checksum:  " << sha_local_hash_text << std::endl;
		std::cout << "remote checksum: " << string_value[2] << std::endl;
	}

	if(int_value[0] != length)
		throw(std::string("checksum failed: checksummed bytes differs, local: ") + std::to_string(length) +  ", remote: " + std::to_string(int_value[0]));

	if(int_value[1] != start)
		throw(std::string("checksum failed: start address differs, local: ") +  std::to_string(start) + ", remote: " + std::to_string(int_value[1]));

	if(string_value[2] != sha_local_hash_text)
		throw(std::string("checksum failed: SHA hash differs, local: ") + sha_local_hash_text + ", remote: " + string_value[2]);

	std::cout << "checksum OK" << std::endl;
}

static void command_verify(GenericSocket &command_channel, GenericSocket &mailbox_channel, const std::string &filename, int start, bool verbose)
{
	int64_t file_offset;
	int fd;
	struct stat stat;
	unsigned int sector_attempt, current, length;
	struct timeval time_start, time_now;
	std::string send_string;
	std::string reply;
	std::string operation;
	std::vector<int> int_value;
	std::vector<std::string> string_value;
	uint8_t sector_buffer[flash_sector_size];

	if(filename.empty())
		throw(std::string("file name required"));

	if((fd = open(filename.c_str(), O_RDONLY)) < 0)
		throw(std::string("can't open file"));

	try
	{
		fstat(fd, &stat);
		file_offset = stat.st_size;
		length = (file_offset + (flash_sector_size - 1)) / flash_sector_size;

		gettimeofday(&time_start, 0);

		std::cout << "start verify from 0x" << std::hex << start * flash_sector_size << " (sector " << std::dec << start << ")";
		std::cout << ", length: " << length * flash_sector_size << " (" << std::dec << length << " sectors)";
		std::cout << std::endl;

		current = start;

		for(current = start; current < (start + length); current++)
		{
			for(sector_attempt = max_attempts; sector_attempt > 0; sector_attempt--)
			{
				send_string = std::string("mailbox-read ") + std::to_string(current);
				process(command_channel, send_string, reply, "OK mailbox-read: sending sector ([0-9]+), checksum: ([0-9a-f]+)", string_value, int_value, verbose);

				if(int_value[0] != (int)current)
					throw(std::string("local address (") + std::to_string(current) + ") != remote address (" + std::to_string(int_value[1]) + ")");

				reply.clear();

				if(mailbox_channel.receive(100, reply, flash_sector_size, true /* raw */))
					break;

				if(!verbose)
					std::cout << std::endl;

				std::cout << "! receive failed, sector #" << current << ", #" << (current - start) << ", attempt #" << max_attempts - sector_attempt;
				std::cout << std::endl;
				usleep(100000);
				mailbox_channel.reconnect();
			}

			if(sector_attempt <= 0)
				throw(std::string("! receiving sector failed too many times"));

			if(verbose)
			{
				std::cout << "+ sector #" << (current - start) << ", " << current;
				std::cout << ", address: 0x" << std::hex << (current * flash_sector_size) << std::dec << " verified";
				std::cout << ", try #" << (max_attempts - sector_attempt);
				std::cout << std::endl;
			}

			if((file_offset = lseek(fd, 0, SEEK_CUR)) < 0)
				throw(std::string("i/o error in seek"));

			memset(sector_buffer, 0xff, sizeof(sector_buffer));

			int count;

			if((count = read(fd, sector_buffer, sizeof(sector_buffer))) <= 0)
				throw(std::string("i/o error in read"));

			if(memcmp(reply.data(), sector_buffer, sizeof(sector_buffer)))
				throw(std::string("data mismatch"));

			if(!verbose)
			{
				int seconds, useconds;
				double duration, rate;

				gettimeofday(&time_now, 0);

				seconds = time_now.tv_sec - time_start.tv_sec;
				useconds = time_now.tv_usec - time_start.tv_usec;
				duration = seconds + (useconds / 1000000.0);
				rate = file_offset / 1024.0 / duration;

				std::cout << std::setfill(' ');
				std::cout << "verified "	<< std::setw(3) << (file_offset / 1024) << " kbytes";
				std::cout << " in "			<< std::setw(4) << std::setprecision(2) << std::fixed << duration << " seconds";
				std::cout << " at rate "	<< std::setw(4) << std::setprecision(0) << std::fixed << rate << " kbytes/s";
				std::cout << ", received "	<< std::setw(2) << (current - start) << " sectors";
				std::cout << ", "			<< std::setw(3) << (((file_offset + flash_sector_size) * 100) / (length * flash_sector_size)) << "%       \r";
				std::cout.flush();
			}
		}
	}
	catch(const std::string &e)
	{
		close(fd);
		throw;
	}

	close(fd);

	if(!verbose)
		std::cout << std::endl;

	std::cout << "verify OK" << std::endl;
}

void command_benchmark(GenericSocket &command_channel, GenericSocket &mailbox_channel, bool verbose)
{
	unsigned int length, current, retries;
	unsigned char sector_buffer[flash_sector_size];
	std::string send_string;
	std::string reply;
	struct timeval time_start, time_now;
	std::vector<int> int_value;
	std::vector<std::string> string_value;

	gettimeofday(&time_start, 0);

	std::cout << "start write benchmark" << std::endl;

	length = 1024;

	process(command_channel, "mailbox-reset", reply, "OK mailbox-reset", string_value, int_value, verbose);

	for(current = 0; current < length; current++)
	{
		memset(sector_buffer, 0x00, flash_sector_size);

		if(!mailbox_channel.send(100, std::string((const char *)sector_buffer, flash_sector_size), true /* raw */))
			throw(std::string("send failed"));

		if(!mailbox_channel.receive(100, reply, 1, true /* raw */))
			throw(std::string("receive failed"));

		send_string = std::string("mailbox-bench 1");
		process(command_channel, send_string, reply, "OK mailbox-bench: received one sector", string_value, int_value, verbose);

		int seconds, useconds;
		double duration, rate;

		gettimeofday(&time_now, 0);

		seconds = time_now.tv_sec - time_start.tv_sec;
		useconds = time_now.tv_usec - time_start.tv_usec;
		duration = seconds + (useconds / 1000000.0);
		rate = current * 4.0 / duration;

		if(!verbose)
		{
			std::cout << std::setfill(' ');
			std::cout << "sent     "		<< std::setw(4) << (current * flash_sector_size / 1024) << " kbytes";
			std::cout << " in "			<< std::setw(5) << std::setprecision(2) << std::fixed << duration << " seconds";
			std::cout << " at rate "	<< std::setw(4) << std::setprecision(0) << std::fixed << rate << " kbytes/s";
			std::cout << ", sent "		<< std::setw(4) << (current + 1) << " sectors";
			std::cout << ", "			<< std::setw(3) << (((current + 1) * 100) / length) << "%       \r";
			std::cout.flush();
		}
	}

	std::cout << std::endl << "write benchmark completed" << std::endl;

	usleep(500000);

	process(command_channel, "mailbox-reset", reply, "OK mailbox-reset", string_value, int_value, verbose);
	retries = 0;

	for(current = 0; current < length; current++)
	{
		try
		{
			send_string = std::string("mailbox-bench 0");
			process(command_channel, send_string, reply, "OK mailbox-bench: sending one sector", string_value, int_value, verbose, 100, 1);
		}
		catch(const std::string &e)
		{
			goto error;
		}

		reply.clear();

		if(!mailbox_channel.receive(1000, reply, flash_sector_size, true /* raw */))
			goto error;

		int seconds, useconds;
		double duration, rate;

		gettimeofday(&time_now, 0);

		seconds = time_now.tv_sec - time_start.tv_sec;
		useconds = time_now.tv_usec - time_start.tv_usec;
		duration = seconds + (useconds / 1000000.0);
		rate = current * 4.0 / duration;

		goto report;

error:
		if(current > 0)
			current--;

		mailbox_channel.reconnect();
		mailbox_channel.send(1000, std::string(" "), true);

		retries++;
		process(command_channel, "mailbox-reset", reply, "OK mailbox-reset", string_value, int_value, verbose);

report:
		if(!verbose)
		{
			std::cout << std::setfill(' ');
			std::cout << "received "	<< std::setw(4) << (current * flash_sector_size / 1024) << " kbytes";
			std::cout << " in "			<< std::setw(5) << std::setprecision(2) << std::fixed << duration << " seconds";
			std::cout << " at rate "	<< std::setw(4) << std::setprecision(0) << std::fixed << rate << " kbytes/s";
			std::cout << ", sent "		<< std::setw(4) << (current + 1) << " sectors";
			std::cout << ", retries "	<< std::setw(2) << retries;
			std::cout << ", "			<< std::setw(3) << (((current + 1) * 100) / length) << "%       \r";
			std::cout.flush();
		}
	}

	std::cout << std::endl << "read benchmark completed" << std::endl;
}

void commit_ota(GenericSocket &command_channel, bool verbose, unsigned int flash_slot, bool reset, bool permanent)
{
	std::string send_string;
	std::string reply;
	std::vector<std::string> string_value;
	std::vector<int> int_value;

	send_string = std::string("mailbox-select ") + std::to_string(flash_slot) + " " + (permanent ? "1" : "0");
	process(command_channel, send_string, reply, "OK mailbox-select: slot ([0-9]+), permanent ([0-1])", string_value, int_value, verbose);

	if((unsigned int)int_value[0] != flash_slot)
		throw(std::string("mailbox-select failed, local slot (") + std::to_string(flash_slot) + ") != remote slot (" + std::to_string(int_value[0]) + ")");

	if(int_value[1] != (permanent ? 1 : 0))
		throw(std::string("mailbox-select failed, local permanent != remote permanent"));

	std::cout << "selected ";

	if(!permanent)
		std::cout << "one time";

	std::cout << " boot slot" << std::endl;

	if(!reset)
		return;

	std::cout << "rebooting" << std::endl;
	command_channel.send(1000, std::string("reset"));
	sleep(1);
	command_channel.reconnect();
	std::cout << "reboot finished" << std::endl;

	process(command_channel, "mailbox-info", reply, "OK mailbox function available, slots: 2, current: ([0-9]+), sectors: \\[ ([0-9]+), ([0-9]+) \\]",
			string_value, int_value, verbose);

	if(int_value[0] != (int)flash_slot)
		throw(std::string("boot failed, requested slot: ") + std::to_string(flash_slot) + ", active slot: " + std::to_string(int_value[0]));

	if(!permanent)
	{
		std::cout << "boot succeeded, permanently selecting boot slot: " << flash_slot << std::endl;

		send_string = std::string("mailbox-select ") + std::to_string(flash_slot) + " 1";
		process(command_channel, send_string, reply, "OK mailbox-select: slot ([0-9]+), permanent ([0-1])", string_value, int_value, verbose);

		if((unsigned int)int_value[0] != flash_slot)
			throw(std::string("mailbox-select failed, local slot (") + std::to_string(flash_slot) + ") != remote slot (" + std::to_string(int_value[0]) + ")");

		if(int_value[1] != 1)
			throw(std::string("mailbox-select failed, local permanent != remote permanent"));
	}

	process(command_channel, "stats", reply, "\\s*>\\s*firmware\\s*>\\s*date:\\s*([a-zA-Z0-9: ]+).*", string_value, int_value, verbose, 500);
	std::cout << "firmware version: " << string_value[0] << std::endl;
}

int main(int argc, const char **argv)
{
	po::options_description	options("usage");

	try
	{
		std::string host;
		std::string command_port;
		std::string mailbox_port;
		std::string filename;
		std::string start_string;
		std::string length_string;
		int start;
		unsigned int length;
		unsigned int chunk_size;
		bool use_udp = false;
		bool verbose = false;
		bool nocommit = false;
		bool noreset = false;
		bool notemp = false;
		bool otawrite = false;
		bool cmd_write = false;
		bool cmd_simulate = false;
		bool cmd_verify = false;
		bool cmd_checksum = false;
		bool cmd_benchmark = false;
		bool cmd_read = false;
		bool cmd_info = false;
		unsigned int selected;

		options.add_options()
			("verbose,v",		po::bool_switch(&verbose)->implicit_value(true),					"verbose output")
			("info,I",			po::bool_switch(&cmd_info)->implicit_value(true),					"INFO")
			("read,R",			po::bool_switch(&cmd_read)->implicit_value(true),					"READ")
			("checksum,C",		po::bool_switch(&cmd_checksum)->implicit_value(true),				"CHECKSUM")
			("verify,V",		po::bool_switch(&cmd_verify)->implicit_value(true),					"VERIFY")
			("simulate,S",		po::bool_switch(&cmd_simulate)->implicit_value(true),				"WRITE simulate")
			("write,W",			po::bool_switch(&cmd_write)->implicit_value(true),					"WRITE")
			("benchmark,B",		po::bool_switch(&cmd_benchmark)->implicit_value(true),				"BENCHMARK")
			("host,h",			po::value<std::string>(&host)->required(),							"host to connect to")
			("udp,u",			po::bool_switch(&use_udp)->implicit_value(true),					"use UDP instead of TCP")
			("filename,f",		po::value<std::string>(&filename),									"file name")
			("start,s",			po::value<std::string>(&start_string)->default_value("-1"),			"send/receive start address (OTA is default)")
			("length,l",		po::value<std::string>(&length_string)->default_value("0x1000"),	"read length")
			("command-port,p",	po::value<std::string>(&command_port)->default_value("24"),			"command port to connect to")
			("mailbox-port,P",	po::value<std::string>(&mailbox_port)->default_value("26"),			"mailbox port to connect to")
			("nocommit,n",		po::bool_switch(&nocommit)->implicit_value(true),					"don't commit after writing")
			("noreset,N",		po::bool_switch(&noreset)->implicit_value(true),					"don't reset after commit")
			("notemp,t",		po::bool_switch(&notemp)->implicit_value(true),						"don't commit temporarily, commit to flash");

		po::positional_options_description positional_options;

		po::variables_map varmap;
		po::store(po::parse_command_line(argc, argv, options), varmap);
		po::notify(varmap);

		selected = 0;

		if(cmd_write)
			selected++;

		if(cmd_simulate)
			selected++;

		if(cmd_verify)
			selected++;

		if(cmd_checksum)
			selected++;

		if(cmd_benchmark)
			selected++;

		if(cmd_read)
			selected++;

		if(selected > 1)
			throw(std::string("specify one of write/simulate/verify/checksum/read"));

		if(selected == 0)
			cmd_info = true;

		start = -1;
		chunk_size = use_udp ? 1024 : flash_sector_size;

		try
		{
			start = std::stoi(start_string, 0, 0);
		}
		catch(...)
		{
			throw(std::string("invalid value for start argument"));
		}

		if(start != -1)
		{
			if(((start % flash_sector_size) != 0) || (start < 0))
				throw(std::string("invalid start address"));
			start /= flash_sector_size;
		}

		try
		{
			length = std::stoi(length_string, 0, 0);
		}
		catch(...)
		{
			throw(std::string("invalid value for length argument"));
		}

		if((length % flash_sector_size) != 0)
			length = length / flash_sector_size + 1;
		else
			length = length / flash_sector_size;

		std::string reply;
		std::vector<int> int_value;
		std::vector<std::string> string_value;
		unsigned int flash_slot, flash_address[2];

		GenericSocket command_channel(host, command_port, use_udp, verbose);
		GenericSocket mailbox_channel(host, mailbox_port, use_udp, verbose);
		mailbox_channel.send(1000, std::string(" "), true);

		try
		{
			process(command_channel, "mailbox-info", reply, "OK mailbox function available, slots: 2, current: ([0-9]+), sectors: \\[ ([0-9]+), ([0-9]+) \\]",
					string_value, int_value, verbose);
		}
		catch(std::string &e)
		{
			throw(std::string("MAILBOX incompatible image: ") + e);
		}

		flash_slot = int_value[0];
		flash_address[0] = int_value[1];
		flash_address[1] = int_value[2];

		std::cout << "MAILBOX update available, current slot: " << flash_slot;
		std::cout << ", address[0]: 0x" << std::hex << (flash_address[0] * flash_sector_size) << " (sector " << std::dec << flash_address[0] << ")";
		std::cout << ", address[1]: 0x" << std::hex << (flash_address[1] * flash_sector_size) << " (sector " << std::dec << flash_address[1] << ")";
		std::cout << std::endl;

		if(start == -1)
		{
			if(cmd_write || cmd_simulate || cmd_verify || cmd_checksum || cmd_info)
			{
				flash_slot++;

				if(flash_slot >= 2)
					flash_slot = 0;

				start = flash_address[flash_slot];
				otawrite = true;
			}
			else
				if(!cmd_benchmark)
					throw(std::string("start address not set"));
		}

		if(cmd_read)
			command_read(command_channel, mailbox_channel, filename, start, length, verbose);
		else
			if(cmd_verify)
				command_verify(command_channel, mailbox_channel, filename, start, verbose);
			else
				if(cmd_checksum)
					command_checksum(command_channel, filename, start, verbose);
				else
					if(cmd_simulate)
						command_write(command_channel, mailbox_channel, filename, start,
								chunk_size, verbose, true, false);
					else
						if(cmd_write)
						{
							command_write(command_channel, mailbox_channel, filename, start,
									chunk_size, verbose, false, otawrite);

							if(otawrite && !nocommit)
								commit_ota(command_channel, verbose, flash_slot, !noreset, notemp);
						}
						else
							if(cmd_benchmark)
								command_benchmark(command_channel, mailbox_channel, verbose);
	}
	catch(const po::error &e)
	{
		std::cerr << std::endl << "espmbf: " << e.what() << std::endl << options;
		return(1);
	}
	catch(const std::exception &e)
	{
		std::cerr << std::endl << "espmbf: " << e.what() << std::endl;
		return(1);
	}
	catch(const std::string &e)
	{
		std::cerr << std::endl << "espmbf: " << e << std::endl;
		return(1);
	}
	catch(...)
	{
		std::cerr << std::endl << "espmbf: unknown exception caught" << std::endl;
		return(1);
	}

	return(0);
}
