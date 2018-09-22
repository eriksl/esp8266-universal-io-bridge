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

#include <openssl/sha.h>

enum
{
	max_attempts = 4,
	max_udp_packet_size = 1472,
};

typedef enum
{
	action_none,
	action_read,
	action_checksum,
	action_verify,
	action_simulate,
	action_write
} action_t;

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

		bool send(int timeout_msec, std::string buffer);
		bool receive(int timeout_msec, std::string &buffer);
		void reconnect();
};

void GenericSocket::reconnect()
{
    struct addrinfo hints;
    struct addrinfo *res;
	struct sockaddr_in6 saddr;

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

bool GenericSocket::send(int timeout, std::string buffer)
{
	struct pollfd pfd;
	ssize_t chunk;

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

bool GenericSocket::receive(int timeout, std::string &reply)
{
	int length;
	int run;
	struct pollfd pfd = { .fd = fd, .events = POLLIN | POLLERR | POLLHUP, .revents = 0 };
	char buffer[8192];

	for(run = 2; run > 0; run--)
	{
		if(poll(&pfd, 1, timeout) != 1)
			return(false);

		if(pfd.revents & (POLLERR | POLLHUP))
			return(false);

		if((length = read(fd, buffer, sizeof(buffer) - 1)) <= 0)
			return(false);

		if(!use_udp || (length != 1) || (buffer[0] != '\0'))
			break;
	}

	reply.assign(buffer, length);

	if(reply.back() == '\n')
		reply.pop_back();

	if(reply.back() == '\r')
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
		std::vector<std::string> &string_value, std::vector<int> &int_value, bool verbose, int timeout = 2000)
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

	for(attempt = max_attempts; attempt > 0; attempt--)
	{
		bool send_status, receive_status;

		send_status = channel.send(timeout, send_string);

		if(send_status)
			receive_status = channel.receive(timeout, reply_string);
		else
			receive_status = false;

		if(send_status && receive_status)
			break;

		std::cout << (!send_status ? "send" : "receive") << " failed, retry #" << (max_attempts - attempt) << std::endl;

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

void command_write(GenericSocket &channel, int fd,
		uint64_t file_length, unsigned int start,
		int flash_sector_size, int chunk_size,
		bool verbose, action_t action, bool erase_before_write)
{
	int64_t file_offset;
	unsigned char sector_buffer[flash_sector_size];
	unsigned char sector_hash[SHA_DIGEST_LENGTH];
	unsigned char file_hash[SHA_DIGEST_LENGTH];
	int sector, sector_length, sector_attempt;
	int chunk_offset, chunk_attempt;
	int current, checksummed;
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

	gettimeofday(&time_start, 0);

	operation = action == action_simulate ? "simulate" : (action == action_verify ? "verify" : "write");

	std::cout << "start " << operation << ", at address: 0x" << std::hex << std::setw(6) << std::setfill('0') << start << ", length: " << std::dec << std::setw(0) << file_length
			<< ", flash buffer size: " << flash_sector_size << ", chunk size: " << chunk_size << std::endl;

	if(action != action_simulate)
		SHA1_Init(&sha_file_ctx);

	if((action == action_write) && (erase_before_write))
	{
		std::cout << "erasing " << std::dec << std::setw(0) << file_length <<
					" bytes from 0x" << std::hex << std::setw(6) << std::setfill('0') << start <<
					std::dec << std::setw(0) << std::endl;

		send_string = "flash-erase " + std::to_string(start) + " " + std::to_string(file_length);

		process(channel, send_string, reply, "OK flash-erase: erased ([0-9]+) sectors from sector ([0-9]+), in ([0-9]+) milliseconds", string_value, int_value, verbose, 10000);

		sectors_erased = file_length / 4096;
		current = start / 4096;

		if((start % 4096) != 0)
		{
			current--;
			sectors_erased++;
		}

		if((file_length % 4096) != 0)
			sectors_erased++;

		if(int_value[0] != sectors_erased)
			throw(std::string("flash-erase: erased sectors count don't match"));

		if(int_value[1] != current)
			throw(std::string("flash-erase: offset of erased sectors don't match"));

		std::cout << "erase finished in " << int_value[2] << " milliseconds" << std::endl;
	}

	sector = 0;
	sectors_written = 0;
	sectors_skipped = 0;
	sectors_erased = 0;
	current = start;
	checksummed = 0;

	while(true)
	{
		memset(sector_buffer, 0xff, flash_sector_size);

		if((sector_length = read(fd, sector_buffer, flash_sector_size)) < 0)
			throw(std::string("i/o error in read"));

		if((file_offset = lseek(fd, 0, SEEK_CUR)) < 0)
			throw(std::string("i/o error in seek"));

		if(sector_length == 0)
			break;

		SHA1(sector_buffer, flash_sector_size, sector_hash);
		sha_local_hash_text = sha_hash_to_text(sector_hash);

		if(action != action_simulate)
		{
			SHA1_Update(&sha_file_ctx, sector_buffer, flash_sector_size);
			checksummed += flash_sector_size;
		}

		for(sector_attempt = max_attempts; sector_attempt > 0; sector_attempt--)
		{
			if(verbose)
				std::cout << "sending sector: " << (file_offset * 1.0 / flash_sector_size)
					<< " (offset: " << file_offset << "), length: " << sector_length << ", try #" << (max_attempts - sector_attempt) << std::endl;

			for(chunk_offset = 0; chunk_offset < (int)flash_sector_size; chunk_offset += chunk_size)
			{
				for(chunk_attempt = max_attempts; chunk_attempt > 0; chunk_attempt--)
				{
					try
					{
						if(verbose)
							std::cout << "sending chunk: " << chunk_offset / chunk_size << " (offset " << (file_offset / flash_sector_size) - 1
									<< " length: " << chunk_size << ", try #" << max_attempts - chunk_attempt << std::endl;

						send_string = "flash-send " + std::to_string(chunk_offset) + " " + std::to_string(chunk_size) + " ";
						send_string.append((const char *)&sector_buffer[chunk_offset], chunk_size);

						process(channel, send_string, reply, "OK flash-send: received bytes: ([0-9]+), at offset: ([0-9]+)\\s*", string_value, int_value, verbose);

						if(int_value[0] != chunk_size)
							throw(std::string("local chunk size (") + std::to_string(chunk_size) + ") != remote chunk size (" + std::to_string(int_value[0]) + ")");

						if(int_value[1] != chunk_offset)
							throw(std::string("local chunk offset (")  + std::to_string(chunk_offset) + ") != remote chunk offset (" + std::to_string(int_value[1]) + ")");

						break;
					}
					catch(const std::string &e)
					{
						if(!verbose)
							std::cout << std::endl;

						std::cout << "! send chunk failed: " << e;
						std::cout << ", sector " << sector << "/" << file_length / flash_sector_size;
						std::cout << ", chunk " << chunk_offset / chunk_size;
						std::cout << ", attempt #" << max_attempts - chunk_attempt;
						std::cout << std::endl;
					}
				}

				if(chunk_attempt == 0)
					throw(std::string("sending chunk failed too many times"));
			}

			if(action != action_simulate)
			{
				try
				{
					if(action == action_verify)
					{
						if(verbose)
							std::cout << "verify sector at 0x" << std::hex << std::setw(6) << std::setfill('0') << current << std::dec << std::setw(0) << std::endl;

						send_string = std::string("flash-verify ") + std::to_string(current);
						process(channel, send_string, reply, "OK flash-verify: verified bytes: ([0-9]+), at address: ([0-9]+) \\([0-9]+\\), same: (0|1), checksum: ([0-9a-f]+)\\s*", string_value, int_value, verbose);

						sha_remote_hash_text = string_value[3];

						if(verbose)
						{
							std::cout << "sector verified";
							std::cout << ", local hash: " << sha_local_hash_text;
							std::cout << ", remote hash: " << sha_remote_hash_text << std::endl;
						}

						if(int_value[0] != flash_sector_size)
							throw(std::string("local sector size (") + std::to_string(flash_sector_size) + ") != remote sector size (" + std::to_string(int_value[0]) + ")");

						if(int_value[1] != current)
							throw(std::string("local address (") + std::to_string(current) + ") != remote address (" + std::to_string(int_value[1]) + ")");

						if(int_value[2] != 1)
							throw(std::string("no match"));

						if(sha_local_hash_text != sha_remote_hash_text)
							throw(std::string("local hash (") + sha_local_hash_text + ") != remote hash (" + sha_remote_hash_text + ")");

						sectors_skipped++;
					}
					else
					{
						if(verbose)
							std::cout << "writing sector at 0x" << std::hex << std::setw(6) << std::setfill('0') << current << std::dec << std::setw(0) << std::endl;

						send_string = std::string("flash-write ") + std::to_string(current);
						process(channel, send_string, reply, "OK flash-write: written bytes: ([0-9]+), to address: ([0-9]+) \\([0-9]+\\), same: (0|1), erased: (0|1), checksum: ([0-9a-f]+)\\s*", string_value, int_value, verbose);

						sha_remote_hash_text = string_value[4];

						if(verbose)
						{
							std::cout << "sector written";
							std::cout << ", local hash: " << sha_local_hash_text;
							std::cout << ", remote hash: " << sha_remote_hash_text;
							std::cout << ", try #" << (max_attempts - sector_attempt) << std::endl;
						}

						if(int_value[0] != flash_sector_size)
							throw(std::string("local sector size (") + std::to_string(flash_sector_size) +  ") != remote sector size (" + std::to_string(int_value[0]) + ")");

						if(int_value[1] != current)
							throw(std::string("local address (") + std::to_string(current) + ") != remote address (" + std::to_string(int_value[1]) +  ")");

						if(sha_local_hash_text != sha_remote_hash_text)
							throw(std::string("local hash (") + sha_local_hash_text + ") != remote hash (" + sha_remote_hash_text + ")");

						if(int_value[2] == 0)
							sectors_written++;
						else
							sectors_skipped++;

						if(int_value[3] != 0)
							sectors_erased++;
					}

					break;
				}
				catch(const std::string &e)
				{
					if(!verbose)
						std::cout << std::endl;

					if(action == action_verify)
					{
						std::cout << "! verify sector failed: " << e;
						std::cout << ", sector " << sector << "/" << file_length / flash_sector_size;
						std::cout << std::endl;

						throw(std::string("verify failed"));
					}
					else
					{
						std::cout << "! write sector failed: " << e;
						std::cout << ", sector " << sector << "/" << file_length / flash_sector_size;
						std::cout << ", attempt #" << max_attempts - sector_attempt;
						std::cout << std::endl;
					}
				}
			}
			else
			{
				sectors_skipped++;
				break;
			}
		}

		sector++;

		if(sector_attempt <= 0)
			throw(std::string("! sending sector failed too many times"));

		current += flash_sector_size;

		if(verbose)
		{
			switch(action)
			{
				case(action_simulate):
				{
					std::cout << "send sector success at ";
					break;
				}
				case(action_verify):
				{
					std::cout << "verify sector success at ";
					break;
				}
				case(action_write):
				{
					std::cout << "write sector success at ";
					break;
				}
				default:
				{
					break;
				}
			}

			std::cout << current << std::endl;
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
			std::cout << "sent "		<< std::setw(3) << (file_offset / 1024) << " kbytes";
			std::cout << " in "			<< std::setw(4) << std::setprecision(2) << std::fixed << duration << " seconds";
			std::cout << " at rate "	<< std::setw(3) << std::setprecision(0) << std::fixed << rate << " kbytes/s";
			std::cout << ", sent "		<< std::setw(2) << sector << " sectors";
			std::cout << ", written "	<< std::setw(2) << sectors_written << " sectors";
			std::cout << ", erased "	<< std::setw(2) << sectors_erased << " sectors";
			std::cout << ", skipped "	<< std::setw(2) << sectors_skipped << " sectors";
			std::cout << ", "			<< std::setw(3) << ((file_offset * 100) / file_length) << "%       \r";
			std::cout.flush();
		}
	}

	if(!verbose)
		std::cout << std::endl;

	if(action != action_simulate)
	{
		std::cout << "checksumming " << checksummed / flash_sector_size << " sectors..." << std::endl;

		SHA1_Final(file_hash, &sha_file_ctx);
		sha_local_hash_text = sha_hash_to_text(file_hash);

		send_string = std::string("flash-checksum ") + std::to_string(start) + " " + std::to_string(checksummed);
		process(channel, send_string, reply, "OK flash-checksum: checksummed bytes: ([0-9]+), from address: ([0-9]+), checksum: ([0-9a-f]+)\\s*", string_value, int_value, verbose);

		if(verbose)
		{
			std::cout << "local checksum:  " << sha_local_hash_text << std::endl;
			std::cout << "remote checksum: " << string_value[2] << std::endl;
		}

		if(int_value[0] != checksummed)
			throw(std::string("checksum failed: checksummed bytes differs, local: ") + std::to_string(checksummed) + ", remote: " + std::to_string(int_value[0]));

		if((unsigned int)int_value[1] != start)
			throw(std::string("checksum failed: start address differs, local: ") +  std::to_string(start) + ", remote: " + std::to_string(int_value[1]));

		if(string_value[2] != sha_local_hash_text)
			throw(std::string("checksum failed: SHA hash differs, local: ") +  sha_local_hash_text + ", remote: " + string_value[2]);

		std::cout << "checksumming done" << std::endl;
	}

	std::cout << operation << " finished" << std::endl;
}

void command_checksum(GenericSocket &channel, int fd, uint64_t file_length, unsigned int start,
		int flash_sector_size, bool verbose)
{
	int64_t file_offset;
	unsigned char sector_buffer[flash_sector_size];
	unsigned char file_hash[SHA_DIGEST_LENGTH];
	int sector_length, checksummed;
	std::string sha_local_hash_text;
	std::string sha_remote_hash_text;
	std::string send_string;
	std::string reply;
	std::string operation;
	std::vector<int> int_value;
	std::vector<std::string> string_value;
	SHA_CTX sha_file_ctx;

	std::cout << "start checksum, file length: " << file_length << ", flash buffer size: " << flash_sector_size << std::endl;

	SHA1_Init(&sha_file_ctx);

	checksummed = 0;

	while(true)
	{
		memset(sector_buffer, 0xff, flash_sector_size);

		if((sector_length = read(fd, sector_buffer, flash_sector_size)) < 0)
			throw(std::string("i/o error in read"));

		if(sector_length == 0)
			break;

		if((file_offset = lseek(fd, 0, SEEK_CUR)) < 0)
			throw(std::string("i/o error in seek"));

		SHA1_Update(&sha_file_ctx, sector_buffer, flash_sector_size);
		checksummed += flash_sector_size;
	}

	std::cout << "checksumming " << checksummed / flash_sector_size << " sectors..." << std::endl;

	SHA1_Final(file_hash, &sha_file_ctx);
	sha_local_hash_text = sha_hash_to_text(file_hash);

	send_string = std::string("flash-checksum ") + std::to_string(start) + " " + std::to_string(checksummed);
	process(channel, send_string, reply, "OK flash-checksum: checksummed bytes: ([0-9]+), from address: ([0-9]+), checksum: ([0-9a-f]+)\\s*", string_value, int_value, verbose);

	if(verbose)
	{
		std::cout << "local checksum:  " << sha_local_hash_text << std::endl;
		std::cout << "remote checksum: " << string_value[2] << std::endl;
	}

	if(int_value[0] != checksummed)
		throw(std::string("checksum failed: checksummed bytes differs, local: ") + std::to_string(checksummed) + ", remote: " + std::to_string(int_value[0]));

	if((unsigned int)int_value[1] != start)
		throw(std::string("checksum failed: start address differs, local: ") +  std::to_string(start) + ", remote: " + std::to_string(int_value[1]));

	if(string_value[2] != sha_local_hash_text)
		throw(std::string("checksum failed: SHA hash differs, local: ") + sha_local_hash_text + ", remote: " + string_value[2]);

	std::cout << "checksumming done" << std::endl;
}

void command_read(GenericSocket &channel, int fd, int start, int length, int flash_sector_size, int chunk_size, bool verbose)
{
	int64_t file_offset;
	unsigned char sector_buffer[flash_sector_size];
	unsigned char sector_hash[SHA_DIGEST_LENGTH];
	unsigned char file_hash[SHA_DIGEST_LENGTH];
	int sector, sector_buffer_length, sector_attempt;
	int chunk_offset, chunk_attempt;
	uint64_t data_offset;
	int current, checksummed;
	struct timeval time_start, time_now;
	std::string sha_local_hash_text;
	std::string sha_remote_hash_text;
	std::string send_string;
	std::string reply;
	std::string operation;
	std::vector<int> int_value;
	std::vector<std::string> string_value;
	SHA_CTX sha_file_ctx;

	gettimeofday(&time_start, 0);

	std::cout << "start read from " << start << ", length: " << length << ", flash buffer size: " << flash_sector_size << ", chunk size: " << chunk_size << std::endl;

	SHA1_Init(&sha_file_ctx);

	sector = 0;
	current = start;
	checksummed = 0;

	for(current = start; current < (start + length); current += flash_sector_size)
	{
		for(sector_attempt = max_attempts; sector_attempt > 0; sector_attempt--)
		{
			if(verbose)
				std::cout << "receiving sector: " << sector << ", length: " << flash_sector_size << ", try #" << (max_attempts - sector_attempt) << std::endl;

			send_string = std::string("flash-read ") + std::to_string(current);
			process(channel, send_string, reply, "OK flash-read: read bytes: ([0-9]+), from address: ([0-9]+) \\([0-9]+\\), checksum: ([0-9a-f]+)", string_value, int_value, verbose);

			if(int_value[0] != flash_sector_size)
				throw(std::string("local sector size (") + std::to_string(flash_sector_size) + ") != remote sector size (" + std::to_string(int_value[0]) + ")");

			if(int_value[1] != current)
				throw(std::string("local address (") + std::to_string(current) + ") != remote address (" + std::to_string(int_value[1]) + ")");

			sha_remote_hash_text = string_value[2];

			for(chunk_offset = 0; chunk_offset < (int)flash_sector_size; chunk_offset += chunk_size)
			{
				for(chunk_attempt = max_attempts; chunk_attempt > 0; chunk_attempt--)
				{
					try
					{
						if(verbose)
							std::cout << "receiving chunk: " << chunk_offset / chunk_size << " (offset " << chunk_offset
									<< ", length: " << chunk_size << ", try #" << max_attempts - chunk_attempt << std::endl;

						send_string = std::string("flash-receive ") + std::to_string(chunk_offset) + " " + std::to_string(chunk_size);
						process(channel, send_string, reply, "OK flash-receive: sending bytes: ([0-9]+), from offset: ([0-9]+), data: @.*", string_value, int_value, verbose);

						if(int_value[0] != chunk_size)
							throw(std::string("local chunk length (") + std::to_string(chunk_size) + ") != remote chunk length (" + std::to_string(int_value[0]) + ")");

						if(int_value[1] != chunk_offset)
							throw(std::string("local chunk offset (") + std::to_string(chunk_offset) + ") != remote chunk offset (" + std::to_string(int_value[1]) + ")");

						if((data_offset = reply.find('@')) == std::string::npos)
							throw(std::string("data offset could not be found"));

						memcpy(sector_buffer + chunk_offset, reply.data() + data_offset + 1, chunk_size);

						break;
					}
					catch(const std::string &e)
					{
						if(!verbose)
							std::cout << std::endl;

						std::cout << "! receive chunk failed: " << e;
						std::cout << ", sector " << sector << "/" << length / flash_sector_size;
						std::cout << ", chunk " << chunk_offset / chunk_size;
						std::cout << ", attempt #" << max_attempts - chunk_attempt;
						std::cout << std::endl;
					}
				}

				if(chunk_attempt <= 0)
					throw(std::string("sending chunk failed too many times"));
			}

			SHA1(sector_buffer, flash_sector_size, sector_hash);
			sha_local_hash_text = sha_hash_to_text(sector_hash);

			if(verbose)
			{
				std::cout << "sector " << sector << " read";
				std::cout << ", local hash: " << sha_local_hash_text;
				std::cout << ", remote hash: " << sha_remote_hash_text;
				std::cout << ", try #" << (max_attempts - sector_attempt) << std::endl;
			}

			if(sha_local_hash_text != sha_remote_hash_text)
				throw(std::string("local hash (") + sha_local_hash_text + ") != remote hash (" + sha_remote_hash_text + ")");

			break;
		}

		if(sector_attempt <= 0)
			throw(std::string("! receiving sector failed too many times"));

		if((file_offset = lseek(fd, 0, SEEK_CUR)) < 0)
			throw(std::string("i/o error in seek"));

		if((int)(file_offset + flash_sector_size) < length)
			sector_buffer_length = flash_sector_size;
		else
			sector_buffer_length = length - file_offset;

		if(write(fd, sector_buffer, sector_buffer_length) <= 0)
			throw(std::string("i/o error in write"));

		SHA1_Update(&sha_file_ctx, sector_buffer, flash_sector_size);
		checksummed += flash_sector_size;

		sector++;

		if(verbose)
			std::cout << "receive sector success at " << current << std::endl;
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
			std::cout << "received "	<< std::setw(3) << (file_offset / 1024) << " kbytes";
			std::cout << " in "			<< std::setw(4) << std::setprecision(2) << std::fixed << duration << " seconds";
			std::cout << " at rate "	<< std::setw(3) << std::setprecision(0) << std::fixed << rate << " kbytes/s";
			std::cout << ", received "	<< std::setw(2) << sector << " sectors";
			std::cout << ", "			<< std::setw(3) << ((file_offset * 100) / length) << "%       \r";
			std::cout.flush();
		}
	}

	std::cout << "checksumming " << checksummed / flash_sector_size << " sectors..." << std::endl;

	SHA1_Final(file_hash, &sha_file_ctx);
	sha_local_hash_text = sha_hash_to_text(file_hash);

	send_string = std::string("flash-checksum ") + std::to_string(start) +  " " + std::to_string(checksummed);
	process(channel, send_string, reply, "OK flash-checksum: checksummed bytes: ([0-9]+), from address: ([0-9]+), checksum: ([0-9a-f]+)\\s*", string_value, int_value, verbose);

	if(verbose)
	{
		std::cout << "local checksum:  " << sha_local_hash_text << std::endl;
		std::cout << "remote checksum: " << string_value[2] << std::endl;
	}

	if(int_value[0] != checksummed)
		throw(std::string("checksum failed: checksummed bytes differs, local: ") + std::to_string(checksummed) +  ", remote: " + std::to_string(int_value[0]));

	if(int_value[1] != start)
		throw(std::string("checksum failed: start address differs, local: ") +  std::to_string(start) + ", remote: " + std::to_string(int_value[1]));

	if(string_value[2] != sha_local_hash_text)
		throw(std::string("checksum failed: SHA hash differs, local: ") + sha_local_hash_text + ", remote: " + string_value[2]);

	std::cout << "checksumming done" << std::endl;
}

int main(int argc, const char **argv)
{
	po::options_description	options("usage");
	int fd = -1;

	try
	{
		std::string host;
		std::string port;
		std::string filename;
		std::string start_string;
		std::string length_string;
		std::string chunk_size_string;
		unsigned int start, length, chunk_size;
		bool use_udp = false;
		bool verbose = false;
		bool verbose2 = false;
		bool nocommit = false;
		bool noreset = false;
		bool notemp = false;
		bool otawrite = false;
		bool erase_before_write = false;
		bool cmd_write = false;
		bool cmd_simulate = false;
		bool cmd_verify = false;
		bool cmd_checksum = false;
		bool cmd_read = false;
		action_t action;

		options.add_options()
			("checksum,C",	po::bool_switch(&cmd_checksum)->implicit_value(true),				"CHECKSUM")
			("chunksize,c",	po::value<std::string>(&chunk_size_string)->default_value("0"),		"send/receive chunk size")
			("erase,e",		po::bool_switch(&erase_before_write)->implicit_value(true),			"erase before write (instead of during write)")
			("filename,f",	po::value<std::string>(&filename),									"file name")
			("host,h",		po::value<std::string>(&host)->required(),							"host to connect to")
			("length,l",	po::value<std::string>(&length_string)->default_value("0x1000"),	"read length")
			("nocommit,n",	po::bool_switch(&nocommit)->implicit_value(true),					"don't commit after writing")
			("noreset,N",	po::bool_switch(&noreset)->implicit_value(true),					"don't reset after commit")
			("notemp,t",	po::bool_switch(&notemp)->implicit_value(true),						"don't commit temporarily, commit to flash")
			("port,p",		po::value<std::string>(&port)->default_value("24"),					"port to connect to")
			("start,s",		po::value<std::string>(&start_string)->default_value("2147483647"),	"send/receive start address")
			("read,R",		po::bool_switch(&cmd_read)->implicit_value(true),					"READ")
			("simulate,S",	po::bool_switch(&cmd_simulate)->implicit_value(true),				"WRITE simulate")
			("udp,u",		po::bool_switch(&use_udp)->implicit_value(true),					"use UDP instead of TCP")
			("verbose,v",	po::bool_switch(&verbose)->implicit_value(true),					"verbose output")
			("verbose2,x",	po::bool_switch(&verbose2)->implicit_value(true),					"less verbose output")
			("verify,V",	po::bool_switch(&cmd_verify)->implicit_value(true),					"VERIFY")
			("write,W",		po::bool_switch(&cmd_write)->implicit_value(true),					"WRITE");

		po::positional_options_description positional_options;

		po::variables_map varmap;
		po::store(po::parse_command_line(argc, argv, options), varmap);
		po::notify(varmap);

		if(cmd_checksum)
			action = action_checksum;
		else
			if(cmd_verify)
				action = action_verify;
			else
				if(cmd_simulate)
					action = action_simulate;
				else
					if(cmd_write)
						action = action_write;
					else
						if(cmd_read)
							action = action_read;
						else
							action = action_none;

		start = 0;
		chunk_size = 0;

		try
		{
			chunk_size = std::stoi(chunk_size_string, 0, 0);
		}
		catch(...)
		{
			throw(std::string("invalid value for chunk size argument"));
		}

		try
		{
			start = std::stoi(start_string, 0, 0);
		}
		catch(...)
		{
			throw(std::string("invalid value for start argument"));
		}

		try
		{
			length = std::stoi(length_string, 0, 0);
		}
		catch(...)
		{
			throw(std::string("invalid value for length argument"));
		}

		std::string reply;
		std::vector<int> int_value;
		std::vector<std::string> string_value;
		unsigned int flash_sector_size, flash_ota, flash_slots, flash_slot;
		unsigned int flash_address[4];
		unsigned int preferred_chunk_size;

		GenericSocket channel(host, port, use_udp, verbose);

		try
		{
			process(channel, "flash-info", reply, "OK [^,]+, sector size: ([0-9]+)[^,]+, OTA update available: ([0-9]+), "
						"slots: ([0-9]+), slot: ([0-9]+), "
						"address: ([0-9]+), address: ([0-9]+), address: ([0-9]+), address: ([0-9]+)"
						"(?:, preferred chunk size: ([0-9]+))?"
						"\\s*",
						string_value, int_value, verbose);
		}
		catch(std::string &e)
		{
			throw(std::string("incompatible image: ") + e);
		}

		flash_sector_size = int_value[0];
		flash_ota = int_value[1];
		flash_slots = int_value[2];
		flash_slot = int_value[3];
		flash_address[0] = int_value[4];
		flash_address[1] = int_value[5];
		flash_address[2] = int_value[6];
		flash_address[3] = int_value[7];
		preferred_chunk_size = int_value[8];

		std::cout << "flash operations available, sector size: " << flash_sector_size;

		if(flash_ota)
			std::cout << ", OTA update available, slots: " << flash_slots << ", current slot: " << flash_slot
					<< ", address[0]: 0x" << std::setw(6) << std::setfill('0') << std::hex << flash_address[0]
					<< ", address[1]: 0x" << std::setw(6) << std::setfill('0') << std::hex << flash_address[1]
					<< ", address[2]: 0x" << std::setw(6) << std::setfill('0') << std::hex << flash_address[2]
					<< ", address[3]: 0x" << std::setw(6) << std::setfill('0') << std::hex << flash_address[3]
					<< ", preferred chunk size: " << std::setw(0) << std::setfill(' ') << std::dec << preferred_chunk_size << std::endl;
		else
			std::cout << ", OTA update NOT available" << std::endl;

		if(chunk_size == 0)
			chunk_size = preferred_chunk_size;

		if(chunk_size == 0)
			chunk_size = 512;

		if((flash_sector_size % chunk_size) != 0)
			throw(std::string("chunk size should be dividable by flash sector size"));

		if(start == 2147483647)
		{
			if(flash_ota)
			{
				if(action == action_write)
				{
					flash_slot++;

					if(flash_slot >= flash_slots)
						flash_slot = 0;
				}

				start = flash_address[flash_slot];
				otawrite = true;
			}
			else
				throw(std::string("no start address supplied and image does not support OTA updating"));
		}

		if((start % flash_sector_size) != 0)
			throw(std::string("start address should be dividable by flash sector size"));

		int64_t file_length = 0;

		if((action != action_none) && (action != action_read))
		{
			struct stat stat;

			if(filename.empty())
				throw(std::string("file name required"));

			if((fd = open(filename.c_str(), O_RDONLY, 0)) < 0)
				throw(std::string("file not found"));

			fstat(fd, &stat);

			file_length = stat.st_size;
		}

		if(action == action_read)
		{
			if(filename.empty())
				throw(std::string("file name required"));

			if((fd = open(filename.c_str(), O_WRONLY | O_TRUNC | O_CREAT, 0777)) < 0)
				throw(std::string("can't create file"));

			file_length = 0;
		}

		switch(action)
		{
			case(action_read):
			{
				command_read(channel, fd, start, length, flash_sector_size, chunk_size, verbose);
				break;
			}

			case(action_checksum):
			{
				command_checksum(channel, fd, file_length, start, flash_sector_size, verbose);
				break;
			}

			case(action_write):
			case(action_simulate):
			case(action_verify):
			{
				command_write(channel, fd, file_length, start, flash_sector_size, chunk_size, verbose, action, erase_before_write);
				break;
			}

			case(action_none):
			{
				break;
			}
		}

		if((action == action_write) && otawrite)
		{
			if(!nocommit)
			{
				std::string send_string;
				std::string reply;

				if(notemp)
				{
					send_string = std::string("flash-select ") + std::to_string(flash_slot);
					process(channel, send_string, reply, "OK flash-select: slot ([0-9]+) selected, address ([0-9]+)\\s*", string_value, int_value, verbose || verbose2);
				}
				else
				{
					send_string = std::string("flash-select-once ") + std::to_string(flash_slot);
					process(channel, send_string, reply, "OK flash-select-once: slot ([0-9]+) selected, address ([0-9]+)\\s*", string_value, int_value, verbose ||verbose2);
				}

				if((unsigned int)int_value[0] != flash_slot)
					throw(std::string("flash-select failed, local slot (") + std::to_string(flash_slot) + ") != remote slot (" + std::to_string(int_value[0]) + ")");

				if((unsigned int)int_value[1] != start)
					throw(std::string("flash-select failed, local address (") +  std::to_string(flash_slot) + ") != remote address (" + std::to_string(int_value[0]) + ")");

				if(notemp)
					std::cout << "selected boot slot";
				else
					std::cout << "selected one time boot slot";

				std::cout << ": " << flash_slot << ", address: 0x" << std::hex << std::setw(6) << std::setfill('0') << start << std::dec << std::setw(0) << std::endl;

				if(!noreset)
				{
					std::cout << "rebooting" << std::endl;

					channel.send(1000, std::string("reset"));

					sleep(2);

					channel.reconnect();

					std::cout << "reboot finished" << std::endl;

					if(!notemp)
					{
						process(channel, "flash-info", reply, "OK [^,]+, sector size: ([0-9]+)[^,]+, OTA update available: ([0-9]+), "
									"slots: ([0-9]+), slot: ([0-9]+), "
									"address: ([0-9]+), address: ([0-9]+), address: ([0-9]+), address: ([0-9]+)"
									"(?:, preferred chunk size: ([0-9]+))?"
									"\\s*",
									string_value, int_value, verbose);

						if(int_value[3] != (int)flash_slot)
							std::cout << "boot failed, requested slot: " << flash_slot << ", active slot: " << int_value[3] << std::endl;
						else
						{
							std::cout << "boot succeeded, permanently selecting boot slot: " << flash_slot << ", address: 0x" << std::hex << std::setw(6) << std::setfill('0') << start << std::dec << std::setw(0) << std::endl;

							std::string send_string;
							std::string reply;

							send_string = std::string("flash-select ") + std::to_string(flash_slot);
							process(channel, send_string, reply,
									"OK flash-select: slot ([0-9]+) selected, address ([0-9]+)\\s*",
									string_value, int_value, verbose || verbose2);

							if((unsigned int)int_value[0] != flash_slot)
								throw(std::string("flash-select failed, local slot (") + std::to_string(flash_slot) + ") != remote slot (" + std::to_string(int_value[0]) + ")");

							if((unsigned int)int_value[1] != start)
								throw(std::string("flash-select failed, local address (") + std::to_string(flash_slot) +  ") != remote address (" + std::to_string(int_value[0]) + ")");
						}
					}
				}

				process(channel, "stats", reply, "> firmware version date: ([a-zA-Z0-9: ]+).*", string_value, int_value, verbose, 1000);

				std::cout << "firmware version: " << string_value[0] << std::endl;
			}
		}
	}
	catch(const po::error &e)
	{
		std::cerr << std::endl << "espflash: " << e.what() << std::endl << options;
		goto error;
	}
	catch(const std::exception &e)
	{
		std::cerr << std::endl << "espflash: " << e.what() << std::endl;
		goto error;
	}
	catch(const std::string &e)
	{
		std::cerr << std::endl << "espflash: " << e << std::endl;
		goto error;
	}
	catch(...)
	{
		std::cerr << std::endl << "espflash: unknown exception caught" << std::endl;
		goto error;
	}

	if(fd >= 0)
		close(fd);

	return(0);

error:
	if(fd >= 0)
		close(fd);

	return(1);
}
