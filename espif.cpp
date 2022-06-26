#include "espif.h"

namespace po = boost::program_options;

enum
{
	max_attempts = 16,
	max_udp_packet_size = 1472,
	flash_sector_size = 4096,
};

typedef std::vector<std::string> StringVector;

static const std::string ack_string("ACK");
static const std::string mailbox_info_reply
		("OK mailbox function available, slots: 2, current: ([0-9]+), sectors: \\[ ([0-9]+), ([0-9]+) \\](?:, display: ([0-9]+)x([0-9]+)px@([0-9]+))?.*");
static const std::string mailbox_simulate_reply("OK mailbox-simulate: received sector ([0-9]+), erased: ([0-9]), skipped ([0-9]), checksum: ([0-9a-f]+)");
static const std::string mailbox_write_reply("OK mailbox-write: written sector ([0-9]+), erased: ([0-9]), skipped ([0-9]), checksum: ([0-9a-f]+)");
static const std::string mailbox_checksum_reply("OK mailbox-checksum: checksummed sectors: ([0-9]+), from sector: ([0-9]+), checksum: ([0-9a-f]+)");
static const std::string mailbox_read_reply("OK mailbox-read: sending sector ([0-9]+), checksum: ([0-9a-f]+)");

class GenericSocket
{
	private:

		int socket_fd;
		std::string host;
		std::string service;
		bool use_udp, verbose;
		int multicast;
		struct sockaddr_in saddr;

	public:
		typedef enum
		{
			cooked,
			raw
		} process_t;

		GenericSocket(const std::string &host, const std::string &port, bool use_udp, bool verbose, int multicast = 0);
		~GenericSocket();

		bool send(std::string buffer, process_t how);
		bool receive(std::string &buffer, process_t how, int expected = -1);
		void drain();
		void disconnect();
		void connect();
};

GenericSocket::GenericSocket(const std::string &host_in, const std::string &service_in, bool use_udp_in, bool verbose_in, int multicast_in)
		: socket_fd(-1), service(service_in), use_udp(use_udp_in), verbose(verbose_in), multicast(multicast_in)
{
	memset(&saddr, 0, sizeof(saddr));

	if(multicast_in > 0)
		host = std::string("239.255.255.") + host_in;
	else
		host = host_in;

	this->connect();
}

GenericSocket::~GenericSocket()
{
	this->disconnect();
}

void GenericSocket::connect()
{
	struct addrinfo hints;
	struct addrinfo *res = nullptr;

	if((socket_fd = socket(AF_INET, (use_udp || (multicast > 0)) ? SOCK_DGRAM : SOCK_STREAM, 0)) < 0)
		throw(std::string("socket failed"));

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = use_udp ? SOCK_DGRAM : SOCK_STREAM;
	hints.ai_flags = AI_NUMERICSERV;

	if(getaddrinfo(host.c_str(), service.c_str(), &hints, &res))
	{
		if(res)
			freeaddrinfo(res);
		throw(std::string("unknown host"));
	}

	if(!res || !res->ai_addr)
		throw(std::string("unknown host"));

	saddr = *(struct sockaddr_in *)res->ai_addr;
	freeaddrinfo(res);

	if(multicast > 0)
	{
		struct ip_mreq mreq;
		int arg;

		arg = 3;

		if(setsockopt(socket_fd, IPPROTO_IP, IP_MULTICAST_TTL, &arg, sizeof(arg)))
			throw(std::string("multicast: cannot set mc ttl"));

		arg = 0;

		if(setsockopt(socket_fd, IPPROTO_IP, IP_MULTICAST_LOOP, &arg, sizeof(arg)))
			throw(std::string("multicast: cannot set loopback"));

		mreq.imr_multiaddr = saddr.sin_addr;
		mreq.imr_interface.s_addr = INADDR_ANY;

		if(setsockopt(socket_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)))
			throw(std::string("multicast: cannot join mc group"));
	}
	else
		if(::connect(socket_fd, (const struct sockaddr *)&saddr, sizeof(saddr)))
			throw(std::string("connect failed"));
}

void GenericSocket::disconnect()
{
	if(socket_fd >= 0)
		close(socket_fd);

	socket_fd = -1;
}

bool GenericSocket::send(std::string buffer, GenericSocket::process_t how)
{
	struct pollfd pfd;
	ssize_t chunk;
	int run;

	if(how == raw)
	{
		if(buffer.length() == 0)
		{
			write(socket_fd, buffer.data(), 0, 0);
			return(true);
		}
	}
	else
		buffer += "\n";

	if(multicast > 0)
	{
		chunk = (ssize_t)buffer.length();

		if(use_udp && (chunk > max_udp_packet_size))
			chunk = max_udp_packet_size;

		for(run = multicast; run > 0; run--)
		{
			if(sendto(socket_fd, buffer.data(), chunk, MSG_DONTWAIT, (const struct sockaddr *)&this->saddr, sizeof(this->saddr)) != chunk)
				return(false);

			usleep(200000);
		}

		return(true);
	}

	while(buffer.length() > 0)
	{
		pfd.fd = socket_fd;
		pfd.events = POLLOUT | POLLERR | POLLHUP;
		pfd.revents = 0;

		if(poll(&pfd, 1, 2000) != 1)
			return(false);

		if(pfd.revents & (POLLERR | POLLHUP))
			return(false);

		chunk = (ssize_t)buffer.length();

		if(use_udp && (chunk > max_udp_packet_size))
			chunk = max_udp_packet_size;

		if(write(socket_fd, buffer.data(), chunk) != chunk)
			return(false);

		buffer.erase(0, chunk);
	}

	return(true);
}

bool GenericSocket::receive(std::string &reply, GenericSocket::process_t how, int expected)
{
	int attempt;
	int length;
	struct pollfd pfd = { .fd = socket_fd, .events = POLLIN | POLLERR | POLLHUP, .revents = 0 };
	char buffer[flash_sector_size + 1];
	enum { max_attempts = 4 };

	reply.clear();

	if(multicast > 0)
	{
		struct timeval tv_start, tv_now;
		uint64_t start, now;
		struct sockaddr_in remote_addr;
		socklen_t remote_length;
		char host_buffer[64];
		char service[64];
		std::string hostname;
		std::stringstream text;
		int gai_error;
		std::string line;
		uint32_t host_id;
		typedef struct { int count; std::string hostname; std::string text; } multicast_reply_t;
		typedef std::map<unsigned uint32_t, multicast_reply_t> multicast_replies_t;
		multicast_replies_t multicast_replies;
		int total_replies, total_hosts;

		total_replies = total_hosts = 0;

		gettimeofday(&tv_start, nullptr);
		start = (tv_start.tv_sec * 1000000) + tv_start.tv_usec;

		for(attempt = max_attempts; attempt > 0; attempt--)
		{
			gettimeofday(&tv_now, nullptr);
			now = (tv_now.tv_sec * 1000000) + tv_now.tv_usec;

			if(((now - start) / 1000ULL) > 2000)
				break;

			if(poll(&pfd, 1, 100) != 1)
				continue;

			if(pfd.revents & POLLERR)
				return(false);

			if(pfd.revents & POLLHUP)
				return(false);

			remote_length = sizeof(remote_addr);
			if((length = recvfrom(socket_fd, buffer, sizeof(buffer), 0, (struct sockaddr *)&remote_addr, &remote_length)) < 0)
				return(false);

			attempt = max_attempts;

			if(length == 0)
				continue;

			buffer[length] = '\0';
			line = buffer;

			if((line.back() == '\n'))
				line.pop_back();

			if((line.back() == '\r'))
				line.pop_back();

			host_id = ntohl(remote_addr.sin_addr.s_addr);

			gai_error = getnameinfo((struct sockaddr *)&remote_addr, sizeof(remote_addr), host_buffer, sizeof(host_buffer),
					service, sizeof(service), NI_DGRAM | NI_NUMERICSERV | NI_NOFQDN);

			if(gai_error != 0)
			{
				if(verbose)
					std::cout << "cannot resolve: " << gai_strerror(gai_error) << std::endl;

				hostname = "0.0.0.0";
			}
			else
				hostname = host_buffer;

			total_replies++;

			auto it = multicast_replies.find(host_id);

			if(it != multicast_replies.end())
				it->second.count++;
			else
			{
				total_hosts++;
				multicast_reply_t entry;

				entry.count = 1;
				entry.hostname = hostname;
				entry.text = line;
				multicast_replies[host_id] = entry;
			}
		}

		text.str("");

		for(auto &it : multicast_replies)
		{
			std::stringstream host_id_text;

			host_id_text.str("");

			host_id_text << ((it.first & 0xff000000) >> 24) << ".";
			host_id_text << ((it.first & 0x00ff0000) >> 16) << ".";
			host_id_text << ((it.first & 0x0000ff00) >>  8) << ".";
			host_id_text << ((it.first & 0x000000ff) >>  0);
			text << std::setw(12) << std::left << host_id_text.str();
			text << " " << it.second.count << " ";
			text << std::setw(12) << std::left << it.second.hostname;
			text << " " << it.second.text << std::endl;
		}

		text << std::endl << "Total of " << total_replies << " replies received, " << total_hosts << " hosts" << std::endl;

		reply = text.str();
	}
	else
	{
		for(attempt = max_attempts; attempt > 0; attempt--)
		{
			if(poll(&pfd, 1, 500) != 1)
			{
				if(reply.length() == 0)
				{
					if(verbose)
						std::cout << std::endl << "receive: timeout" << std::endl;
					return(false);
				}

				break;
			}

			if(pfd.revents & POLLERR)
			{
				if(verbose)
					std::cout << "receive: POLLERR" << std::endl;
				return(false);
			}

			if(pfd.revents & POLLHUP)
			{
				if(verbose)
					std::cout << "receive: POLLHUP" << std::endl;
				break;
			}

			if((length = read(socket_fd, buffer, sizeof(buffer) - 1)) < 0)
			{
				if(verbose)
					std::cout << "receive: length < 0" << std::endl;
				return(false);
			}

			if((how == cooked) && (length == 0))
			{
				if(reply.length() == 0)
					continue;

				break;
			}

			reply.append(buffer, (size_t)length);

			if((how == cooked) && (reply.back() == 0x04))
			{
				reply.pop_back();

				if(reply.length() == 0)
					continue;

				break;
			}

			if((expected > 0) && (expected > length))
				expected -= length;
			else
				break;
		}

		if((how == cooked) && (reply.back() == '\n'))
			reply.pop_back();

		if((how == cooked) && (reply.back() == '\r'))
			reply.pop_back();
	}

	return(true);
}

void GenericSocket::drain()
{
	struct pollfd pfd;
	char buffer[flash_sector_size];
	unsigned int attempt;

	for(attempt = max_attempts; attempt > 0; attempt--)
	{
		pfd.fd = socket_fd;
		pfd.events = POLLIN | POLLERR | POLLHUP;
		pfd.revents = 0;

		if(poll(&pfd, 1, 200) != 1)
			return;

		if(pfd.revents & (POLLERR | POLLHUP))
			return;

		if(read(socket_fd, buffer, sizeof(buffer)) < 0)
			return;
	}
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
		std::vector<std::string> &string_value, std::vector<int> &int_value, bool verbose)
{
	boost::regex re(match);
	boost::smatch capture;
	unsigned int captures;
	unsigned int attempt;

	enum { max_attempts  = 5 };

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

		if((send_status = channel.send(send_string, GenericSocket::cooked)))
		{
			receive_status = channel.receive(reply_string, GenericSocket::cooked);

			if(reply_string.length() == 0)
			{
				if(verbose)
					std::cout << std::endl << "process: empty string received" << std::endl;
				receive_status = false;
			}
		}
		else
			receive_status = false;

		if(send_status && receive_status)
			break;

		if(verbose)
			std::cout << std::endl << "process: " << (!send_status ? "send" : "receive") << " failed, retry #" << (max_attempts - attempt) << std::endl;

		channel.drain();
	}

	if(attempt == 0)
		throw(std::string("process: no more tries\n"));

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
	int file_fd;
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
	std::vector<int> int_value;
	std::vector<std::string> string_value;
	SHA_CTX sha_file_ctx;

	sectors_skipped = 0;
	sectors_erased = 0;
	sectors_written = 0;

	if(filename.empty())
		throw(std::string("file name required"));

	if((file_fd = open(filename.c_str(), O_RDONLY, 0)) < 0)
		throw(std::string("file not found"));

	fstat(file_fd, &stat);
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

			if((sector_length = read(file_fd, sector_buffer, flash_sector_size)) <= 0)
				throw(std::string("i/o error in read"));

			if((file_offset = lseek(file_fd, 0, SEEK_CUR)) < 0)
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

					if(!mailbox_channel.send(std::string((const char *)sector_buffer, flash_sector_size), GenericSocket::raw))
						throw(std::string("send failed"));

					if(!mailbox_channel.receive(reply, GenericSocket::raw, ack_string.length()))
						throw(std::string("receive failed"));

					if(verbose)
						std::cout << "mailbox replied: \"" << reply << "\"" << std::endl;

					if(reply != ack_string)
						throw(std::string("ack failed"));

					process(command_channel,
							std::string(simulate ? "mailbox-simulate " : "mailbox-write ") + std::to_string(current), reply,
							simulate ? mailbox_simulate_reply : mailbox_write_reply, string_value, int_value, verbose);

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
						throw(std::string("local hash (") + sha_local_hash_text + ") != remote hash (" + sha_remote_hash_text + ") (2)");

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

					command_channel.drain();
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
				std::cout << " in "			<< std::setw(5) << std::setprecision(2) << std::fixed << duration << " seconds";
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
		close(file_fd);
		throw;
	}

	close(file_fd);

	if(!verbose)
		std::cout << std::endl;

	if(simulate)
		std::cout << "simulate finished" << std::endl;
	else
	{
		std::cout << "checksumming " << length << " sectors..." << std::endl;

		SHA1_Final(file_hash, &sha_file_ctx);
		sha_local_hash_text = sha_hash_to_text(file_hash);

		send_string = std::string("mailbox-checksum ") + std::to_string(start) + " " + std::to_string(length);
		process(command_channel, send_string, reply, mailbox_checksum_reply, string_value, int_value, verbose);

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
	int file_fd;
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

	if((file_fd = open(filename.c_str(), O_RDONLY, 0)) < 0)
		throw(std::string("file not found"));

	fstat(file_fd, &stat);
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

			if((sector_length = read(file_fd, sector_buffer, flash_sector_size)) <= 0)
				throw(std::string("i/o error in read"));

			SHA1_Update(&sha_file_ctx, sector_buffer, flash_sector_size);
		}
	}
	catch(std::string &e)
	{
		close(file_fd);
		throw;
	}

	close(file_fd);

	std::cout << "checksumming " << length << " sectors..." << std::endl;

	SHA1_Final(file_hash, &sha_file_ctx);
	sha_local_hash_text = sha_hash_to_text(file_hash);

	send_string = std::string("mailbox-checksum ") + std::to_string(start) +  " " + std::to_string(length);
	process(command_channel, send_string, reply, mailbox_checksum_reply, string_value, int_value, verbose);

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
	int file_fd;
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

	if((file_fd = open(filename.c_str(), O_WRONLY | O_TRUNC | O_CREAT, 0666)) < 0)
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

				try
				{
					process(command_channel, send_string, reply, mailbox_read_reply, string_value, int_value, verbose);
				}
				catch(const std::string &error)
				{
					if(verbose)
						std::cout << "mailbox read failed: " << error << std::endl;

					goto error;
				}

				if(int_value[0] != current)
					throw(std::string("local address (") + std::to_string(current) + ") != remote address (" + std::to_string(int_value[1]) + ")");

				sha_remote_hash_text = string_value[1];

				reply.clear();

				if(mailbox_channel.receive(reply, GenericSocket::raw, flash_sector_size))
				{
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

					if(sha_local_hash_text == sha_remote_hash_text)
						break;

					if(!verbose)
						std::cout << std::endl;
					std::cout << "! local hash (" << sha_local_hash_text << ") != remote hash (" << sha_remote_hash_text << ") (1)" << std::endl;
				}
error:
				if(verbose)
				{
					std::cout << "! read receive failed, sector #" << current << ", #" << (current - start) << ", attempt #" << max_attempts - sector_attempt;
					std::cout << std::endl;
				}

				command_channel.drain();
			}

			if(sector_attempt <= 0)
				throw(std::string("! receiving sector failed too many times"));

			if((file_offset = lseek(file_fd, 0, SEEK_CUR)) < 0)
				throw(std::string("i/o error in seek"));

			if(write(file_fd, reply.data(), flash_sector_size) <= 0)
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
		close(file_fd);
		throw;
	}

	close(file_fd);

	if(!verbose)
		std::cout << std::endl;

	std::cout << "checksumming " << length << " sectors..." << std::endl;

	SHA1_Final(file_hash, &sha_file_ctx);
	sha_local_hash_text = sha_hash_to_text(file_hash);

	send_string = std::string("mailbox-checksum ") + std::to_string(start) +  " " + std::to_string(length);
	process(command_channel, send_string, reply, mailbox_checksum_reply, string_value, int_value, verbose);

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
	int file_fd;
	struct stat stat;
	unsigned int sector_attempt, current, length;
	struct timeval time_start, time_now;
	std::string send_string;
	std::string reply;
	std::string operation;
	std::vector<int> int_value;
	std::vector<std::string> string_value;
	uint8_t sector_buffer[flash_sector_size];

	unsigned char hash[SHA_DIGEST_LENGTH];
	std::string sha_local_hash_text;
	std::string sha_remote_hash_text;

	if(filename.empty())
		throw(std::string("file name required"));

	if((file_fd = open(filename.c_str(), O_RDONLY)) < 0)
		throw(std::string("can't open file"));

	try
	{
		fstat(file_fd, &stat);
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

				try
				{
					process(command_channel, send_string, reply, mailbox_read_reply, string_value, int_value, verbose);
				}
				catch(const std::string &error)
				{
					if(verbose)
						std::cout << "mailbox read failed: " << error << std::endl;

					goto error;
				}

				if(int_value[0] != (int)current)
					throw(std::string("local address (") + std::to_string(current) + ") != remote address (" + std::to_string(int_value[1]) + ")");

				sha_remote_hash_text = string_value[1];

				reply.clear();

				if(mailbox_channel.receive(reply, GenericSocket::raw, flash_sector_size))
				{
					if(verbose)
					{
						std::cout << "+ sector #" << (current - start) << ", " << current;
						std::cout << ", address: 0x" << std::hex << (current * flash_sector_size) << std::dec << " verified";
						std::cout << ", try #" << (max_attempts - sector_attempt);
						std::cout << std::endl;
					}

					SHA1((const unsigned char *)reply.data(), flash_sector_size, hash);
					sha_local_hash_text = sha_hash_to_text(hash);

					if(sha_remote_hash_text != sha_local_hash_text)
					{
						std::cout << std::endl << "! checksum mismatch, local: " << sha_local_hash_text << ", remote: " << sha_remote_hash_text << std::endl;
						goto error;
					}

					if((file_offset = lseek(file_fd, (current - start) * flash_sector_size, SEEK_SET)) < 0)
						throw(std::string("i/o error in seek"));

					memset(sector_buffer, 0xff, sizeof(sector_buffer));

					if(read(file_fd, sector_buffer, sizeof(sector_buffer)) <= 0)
						throw(std::string("i/o error in read"));

					if(memcmp(reply.data(), sector_buffer, sizeof(sector_buffer)))
					{
						std::cout << std::endl << "! data mismatch" << std::endl;
						goto error;
					}

					break;
				}
error:
				if(verbose)
					std::cout << "! receive failed, sector #" << current << ", #" << (current - start) << ", attempt #" << max_attempts - sector_attempt << std::endl;

				command_channel.drain();
			}

			if(sector_attempt == 0)
				throw(std::string("! verify: receiving sector failed too many times"));

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
		close(file_fd);
		throw;
	}

	close(file_fd);

	if(!verbose)
		std::cout << std::endl;

	std::cout << "verify OK" << std::endl;
}

void command_benchmark(GenericSocket &command_channel, GenericSocket &mailbox_channel, bool verbose)
{
	unsigned int phase, retries, attempt, length, current;
	unsigned char sector_buffer[flash_sector_size];
	std::string reply;
	struct timeval time_start, time_now;
	std::vector<int> int_value;
	std::vector<std::string> string_value;
	int seconds, useconds;
	double duration, rate;

	length = 1024;
	memset(sector_buffer, 0x00, flash_sector_size);

	for(phase = 0; phase < 2; phase++)
	{
		retries = 0;
		gettimeofday(&time_start, 0);

		process(command_channel, "mailbox-reset", reply, "OK mailbox-reset", string_value, int_value, verbose);

		for(current = 0; current < length; current++)
		{
			for(attempt = max_attempts; attempt > 0; attempt--)
			{
				try
				{
					if(phase == 0)
					{
						if(!mailbox_channel.send(std::string((const char *)sector_buffer, flash_sector_size), GenericSocket::raw))
							throw(std::string("send failed"));

						if(!mailbox_channel.receive(reply, GenericSocket::cooked, ack_string.length()))
							throw(std::string("receive failed"));

						if(reply != ack_string)
							throw(std::string("ack failed"));

						process(command_channel, "mailbox-bench 1", reply, "OK mailbox-bench: received one sector", string_value, int_value, verbose);
					}
					else
					{
						process(command_channel, "mailbox-bench 0", reply, "OK mailbox-bench: sending one sector", string_value, int_value, verbose);

						if(!mailbox_channel.receive(reply, GenericSocket::raw, flash_sector_size))
							throw(std::string("receive failed"));
					}

					gettimeofday(&time_now, 0);

					seconds = time_now.tv_sec - time_start.tv_sec;
					useconds = time_now.tv_usec - time_start.tv_usec;
					duration = seconds + (useconds / 1000000.0);
					rate = current * 4.0 / duration;

					if(!verbose)
					{
						std::cout << std::setfill(' ');
						std::cout << ((phase == 0) ? "sent     " : "received ");
						std::cout << std::setw(4) << (current * flash_sector_size / 1024) << " kbytes";
						std::cout << " in "			<< std::setw(5) << std::setprecision(2) << std::fixed << duration << " seconds";
						std::cout << " at rate "	<< std::setw(4) << std::setprecision(0) << std::fixed << rate << " kbytes/s";
						std::cout << ", sent "		<< std::setw(4) << (current + 1) << " sectors";
						std::cout << ", retries "	<< std::setw(2) << retries;
						std::cout << ", "			<< std::setw(3) << (((current + 1) * 100) / length) << "%       \r";
						std::cout.flush();
					}
				}
				catch(const std::string &e)
				{
					if(verbose)
						std::cout << e << std::endl;

					command_channel.drain();
					process(command_channel, "mailbox-reset", reply, "OK mailbox-reset", string_value, int_value, verbose);

					retries++;
					continue;
				}

				break;
			}

			if(attempt == 0)
				throw(std::string("benchmark: no more attempts left"));
		}

		usleep(200000);
		std::cout << std::endl;
	}
}

static void command_image_send_sector(GenericSocket &command_channel, GenericSocket &mailbox_channel,
		int current_sector, const unsigned char *buffer, unsigned int size, unsigned int length,
		unsigned int current_x, unsigned int current_y, unsigned int depth, bool verbose)
{
	enum { attempts =  8 };
	unsigned int attempt;
	std::vector<int> int_value;
	std::vector<std::string> string_value;
	std::string reply;
	unsigned char sector_hash[SHA_DIGEST_LENGTH];
	std::string sha_local_hash_text;
	unsigned int pixels;

	SHA1(buffer, size, sector_hash);
	sha_local_hash_text = sha_hash_to_text(sector_hash);

	for(attempt = 0; attempt < attempts; attempt++)
	{
		try
		{
			if(!mailbox_channel.send(std::string((const char *)buffer, flash_sector_size), GenericSocket::raw))
			{
				if(verbose)
					std::cout << "send failed, attempt: " << attempt << std::endl;

				goto error;
			}

			if(!mailbox_channel.receive(reply, GenericSocket::raw, ack_string.length()) || (reply != ack_string))
			{
				if(verbose)
					std::cout << "receive ack failed, attempt: " << attempt << std::endl;

				goto error;
			}

			if(current_sector < 0)
			{
				switch(depth)
				{
					case(1):
					{
						pixels = length * 8;
						break;
					}

					case(16):
					{
						pixels = length / 2;
						break;
					}

					default:
					{
						throw(std::string("unknown display colour depth"));
					}
				}

				process(command_channel, std::string("display-plot ") +
						std::to_string(pixels) + " " +
						std::to_string(current_x) + " " +
						std::to_string(current_y),
						reply,
						"display plot success: yes",
						string_value, int_value, verbose);
			}
			else
			{
				process(command_channel,
							std::string("mailbox-write ") + std::to_string(current_sector), reply,
							mailbox_write_reply, string_value, int_value, verbose);

				if(int_value[0] != current_sector)
				{
					if(verbose)
						std::cout << "invalid sector: " << int_value[0] << "/" << current_sector << std::endl;

					goto error;
				}

				if(string_value[3] != sha_local_hash_text)
				{
					if(verbose)
						std::cout << "invalid checksum: " << string_value[3] << "/" << sha_local_hash_text << std::endl;

					goto error;
				}
			}
		}
		catch(const std::string &e)
		{
			if(verbose)
				std::cout << "send failed, attempt: " << attempt << ", reason: " << e << std::endl;

			goto error;
		}

		break;

error:
		command_channel.drain();
		process(command_channel, "mailbox-reset", reply, "OK mailbox-reset", string_value, int_value, verbose);
	}

	if(attempt >= attempts)
		throw(std::string("send failed, max attempts reached"));
}

static void command_image(GenericSocket &command_channel, GenericSocket &mailbox_channel, int image_slot, const std::string &filename, unsigned chunk_size,
		unsigned int dim_x, unsigned int dim_y, unsigned int depth, int image_timeout, bool verbose)
{
	std::vector<int> int_value;
	std::vector<std::string> string_value;
	std::string reply;
	unsigned char sector_buffer[flash_sector_size];
	unsigned int start_x, start_y;
	unsigned int current_buffer, x, y, r, g, b, l;
	int current_sector;
	struct timeval time_start, time_now;
	int seconds, useconds;
	double duration, rate;

	gettimeofday(&time_start, 0);

	if(image_slot == 0)
		current_sector = 0x200000 / flash_sector_size;
	else
		if(image_slot == 1)
			current_sector = 0x280000 / flash_sector_size;
		else
			current_sector = -1;

	try
	{
		Magick::Image image;
		Magick::Geometry newsize(dim_x, dim_y);
		Magick::Color colour;
		newsize.aspect(true);

		if(!filename.length())
			throw(std::string("empty file name"));

		image.read(filename);

		if(verbose)
			std::cout << "image loaded from " << filename << ", " << image.columns() << "x" << image.rows() << ", " << image.magick() << std::endl;

		image.resize(newsize);

		if((image.columns() != dim_x) || (image.rows() != dim_y))
			throw(std::string("image magic resize failed"));

		if(image_slot < 0)
			process(command_channel, std::string("display-freeze ") + std::to_string(10000), reply, "display freeze success: yes", string_value, int_value, verbose);

		current_buffer = 0;
		start_x = 0;
		start_y = 0;

		process(command_channel, "mailbox-reset", reply, "OK mailbox-reset", string_value, int_value, verbose);
		memset(sector_buffer, 0xff, flash_sector_size);

		for(y = 0; y < dim_y; y++)
		{
			for(x = 0; x < dim_x; x++)
			{
				colour = image.pixelColor(x, y);

				switch(depth)
				{
					case(1):
					{
						if((current_buffer / 8) + 1 > chunk_size)
						{
							command_image_send_sector(command_channel, mailbox_channel, current_sector, sector_buffer, sizeof(sector_buffer), current_buffer / 8,
									start_x, start_y, depth, verbose);
							memset(sector_buffer, 0xff, flash_sector_size);
							current_buffer -= (current_buffer / 8) * 8;
						}

						l = ((colour.redQuantum() + colour.greenQuantum() + colour.blueQuantum()) / 3) > (1 << 15);

						if(l)
							sector_buffer[current_buffer / 8] |=  (1 << (7 - (current_buffer % 8)));
						else
							sector_buffer[current_buffer / 8] &= ~(1 << (7 - (current_buffer % 8)));

						current_buffer++;

						break;
					}

					case(16):
					{
						unsigned int r1, g1, g2, b1;

						r = colour.redQuantum() >> 11;
						g = colour.greenQuantum() >> 10;
						b = colour.blueQuantum() >> 11;

						if((current_buffer + 2) > chunk_size)
						{
							command_image_send_sector(command_channel, mailbox_channel, current_sector, sector_buffer, sizeof(sector_buffer), current_buffer,
									start_x, start_y, depth, verbose);
							memset(sector_buffer, 0xff, flash_sector_size);

							if(current_sector >= 0)
								current_sector++;

							current_buffer = 0;
							start_x = x;
							start_y = y;
						}

						r1 = (r & 0b00011111) >> 0;
						g1 = (g & 0b00111000) >> 3;
						g2 = (g & 0b00000111) >> 0;
						b1 = (b & 0b00011111) >> 0;

						sector_buffer[current_buffer++] = (r1 << 3) | (g1 >> 0);
						sector_buffer[current_buffer++] = (g2 << 5) | (b1 >> 0);

						break;
					}
				}
			}

			gettimeofday(&time_now, 0);

			seconds = time_now.tv_sec - time_start.tv_sec;
			useconds = time_now.tv_usec - time_start.tv_usec;
			duration = seconds + (useconds / 1000000.0);
			rate = (x * 2 * y) / 1024.0 / duration;

			std::cout << std::setfill(' ');
			std::cout << "sent "		<< std::setw(4) << ((x * 2 * y) / 1024) << " kbytes";
			std::cout << " in "			<< std::setw(5) << std::setprecision(2) << std::fixed << duration << " seconds";
			std::cout << " at rate "	<< std::setw(4) << std::setprecision(0) << std::fixed << rate << " kbytes/s";
			std::cout << ", x "			<< std::setw(3) << x;
			std::cout << ", y "			<< std::setw(3) << y;
			std::cout << ", "			<< std::setw(3) << (x * y * 100) / (dim_x * dim_y) << "%       \r";
			std::cout.flush();
		}

		if(current_buffer > 0)
		{
			if(depth == 1)
			{
				if(current_buffer % 8)
					current_buffer += 8;

				current_buffer /= 8;
			}

			command_image_send_sector(command_channel, mailbox_channel, current_sector, sector_buffer, sizeof(sector_buffer),
					current_buffer, start_x, start_y, depth, verbose);
		}

		std::cout << std::endl;

		if(image_slot < 0)
			process(command_channel, std::string("display-freeze ") + std::to_string(0), reply, "display freeze success: yes", string_value, int_value, verbose);

		if((image_slot < 0) && (image_timeout > 0))
			process(command_channel, std::string("display-freeze ") + std::to_string(image_timeout), reply, "display freeze success: yes", string_value, int_value, verbose);
	}
	catch(const Magick::Error &error)
	{
		throw(std::string("image: load failed: ") + error.what());
	}
	catch(const Magick::Warning &warning)
	{
		std::cout << "image: " << warning.what();
	}
}

static void cie_spi_write(GenericSocket &command_channel, const std::string &data, const std::string &match, bool verbose)
{
	std::vector<int> int_value;
	std::vector<std::string> string_value;
	std::string reply;

	process(command_channel, data, reply, match, string_value, int_value, verbose);
}

static void cie_uc_cmd_data(GenericSocket &command_channel, bool isdata, unsigned int data_value, bool verbose)
{
	std::string data, reply;
	std::stringstream text;

	text.str("");
	text << "spt 17 8 " << std::hex << std::setfill('0') << std::setw(2) << data_value << " 0 0 0 0";
	data = text.str();

	cie_spi_write(command_channel, "sps", "spi start ok", verbose);
	cie_spi_write(command_channel, std::string("iw 1 0 ") + (isdata ? "1" : "0"), std::string("digital output: \\[") + (isdata ? "1" : "0") + "\\]", verbose);
	cie_spi_write(command_channel, data, "spi transmit ok", verbose);
	cie_spi_write(command_channel, "spf", "spi finish ok", verbose);
}

static void cie_uc_cmd(GenericSocket &command_channel, unsigned int cmd, bool verbose)
{
	return(cie_uc_cmd_data(command_channel, false, cmd, verbose));
}

static void cie_uc_data(GenericSocket &command_channel, unsigned int data, bool verbose)
{
	return(cie_uc_cmd_data(command_channel, true, data, verbose));
}

static void cie_uc_data_string(GenericSocket &command_channel, const std::string valuestring, bool verbose)
{
	cie_spi_write(command_channel, "iw 1 0 1", "digital output: \\[1\\]", verbose);
	cie_spi_write(command_channel, "sps", "spi start ok", verbose);
	cie_spi_write(command_channel, std::string("spw 8 ") + valuestring, "spi write ok", verbose);
	cie_spi_write(command_channel, "spt 17 0 0 0 0 0 0 0", "spi transmit ok", verbose);
	cie_spi_write(command_channel, "spf", "spi finish ok", verbose);
}

static void command_image_epaper(GenericSocket &command_channel, const std::string &filename, bool verbose)
{
	static const unsigned int dim_x = 212;
	static const unsigned int dim_y = 104;
	uint8_t dummy_display[dim_x][dim_y];
	std::vector<int> int_value;
	std::vector<std::string> string_value;
	std::string values, command, reply;
	std::stringstream text;
	unsigned int layer, all_bytes, bytes, byte, bit;
	int x, y;
	struct timeval time_start, time_now;
	int seconds, useconds;
	double duration, rate;

	gettimeofday(&time_start, 0);

	cie_spi_write(command_channel, "spc 0 0", "spi configure ok", verbose);

	cie_uc_cmd(command_channel, 0x04, verbose); 	// power on PON, no arguments

	cie_uc_cmd(command_channel, 0x00, verbose);		// panel settings PSR, 1 argument
	cie_uc_data(command_channel, 0x0f, verbose);	// default

	cie_uc_cmd(command_channel, 0x61, verbose); 	// resultion settings TSR, 3 argument
	cie_uc_data(command_channel, 0x68, verbose);	// height
	cie_uc_data(command_channel, 0x00, verbose);	// width[7]
	cie_uc_data(command_channel, 0xd4, verbose);	// width[6-0]

	cie_uc_cmd(command_channel, 0x50, verbose); 	// vcom and data interval setting, 1 argument
	cie_uc_data(command_channel, 0xd7, verbose);	// default

	try
	{
		Magick::Image image;
		Magick::Geometry newsize(dim_x, dim_y);
		Magick::Color colour;
		newsize.aspect(true);

		if(!filename.length())
			throw(std::string("image epaper: empty file name"));

		image.read(filename);

		if(verbose)
			std::cout << "image loaded from " << filename << ", " << image.columns() << "x" << image.rows() << ", " << image.magick() << std::endl;

		image.resize(newsize);

		if((image.columns() != dim_x) || (image.rows() != dim_y))
			throw(std::string("image epaper: image magic resize failed"));

		all_bytes = 0;
		bytes = 0;
		byte = 0;
		bit = 7;
		values = "";
		cie_spi_write(command_channel, "sps", "spi start ok", verbose);

		for(x = 0; x < (int)dim_x; x++)
			for(y = 0; y < (int)dim_y; y++)
				dummy_display[x][y] = 0;

		for(layer = 0; layer < 2; layer++)
		{
			cie_uc_cmd(command_channel, layer == 0 ? 0x10 : 0x13, verbose); // DTM1 / DTM2

			for(x = dim_x - 1; x >= 0; x--)
			{
				for(y = 0; y < (int)dim_y; y++)
				{
					colour = image.pixelColor(x, y);

					if(layer == 0)
					{
						if((colour.redQuantum() > 16384) && (colour.greenQuantum() > 16384) && (colour.blueQuantum() > 16384))
						{
							dummy_display[x][y] |= 0x01;
							byte |= 1 << bit;
						}
					}
					else
					{
						if((colour.redQuantum() > 16384) && (colour.greenQuantum() < 16384) && (colour.blueQuantum() < 16384))
						{
							dummy_display[x][y] |= 0x02;
							byte |= 1 << bit;
						}
					}

					if(bit > 0)
						bit--;
					else
					{
						text.str("");
						text << std::hex << std::setfill('0') << std::setw(2) << byte << " ";
						values.append(text.str());
						all_bytes++;
						bytes++;
						bit = 7;
						byte = 0;

						if(bytes > 31)
						{
							cie_uc_data_string(command_channel, values, verbose);
							values = "";
							bytes = 0;
						}
					}
				}

				gettimeofday(&time_now, 0);

				seconds = time_now.tv_sec - time_start.tv_sec;
				useconds = time_now.tv_usec - time_start.tv_usec;
				duration = seconds + (useconds / 1000000.0);
				rate = all_bytes / 1024.0 / duration;

				std::cout << std::setfill(' ');
				std::cout << "sent "		<< std::setw(4) << (all_bytes / 1024) << " kbytes";
				std::cout << " in "			<< std::setw(5) << std::setprecision(2) << std::fixed << duration << " seconds";
				std::cout << " at rate "	<< std::setw(4) << std::setprecision(0) << std::fixed << rate << " kbytes/s";
				std::cout << ", x "			<< std::setw(3) << x;
				std::cout << ", y "			<< std::setw(3) << y;
				std::cout << ", "			<< std::setw(3) << ((dim_x - 1 - x) * y * 100) / (2 * dim_x * dim_y) << "%       \r";
				std::cout.flush();
			}

			if(bytes > 0)
			{
				cie_uc_data_string(command_channel, values, verbose);
				values = "";
				bytes = 0;
			}

			cie_uc_cmd(command_channel, 0x11, verbose); // data stop DST
		}

		cie_uc_cmd(command_channel, 0x12, verbose); // display refresh DRF
	}
	catch(const Magick::Error &error)
	{
		throw(std::string("image epaper: load failed: ") + error.what());
	}
	catch(const Magick::Warning &warning)
	{
		std::cout << "image epaper: " << warning.what();
	}

	if(verbose)
	{
		for(y = 0; y < 104; y++)
		{
			for(x = 0; x < 200; x++)
			{
				switch(dummy_display[x][y])
				{
					case(0): fputs(" ", stdout); break;
					case(1): fputs("1", stdout); break;
					case(2): fputs("2", stdout); break;
					default: fputs("*", stdout); break;
				}
			}

			fputs("$\n", stdout);
		}
	}
}

static void command_send(const std::string &host, const std::string &port, bool udp, bool verbose, bool dont_wait, std::string args)
{
	std::string reply;
	unsigned int attempt;
	std::string arg;
	size_t current;

	if(dont_wait)
	{
		if(daemon(0, 0))
		{
			perror("daemon");
			return;
		}
	}

	GenericSocket send_socket(host, port, udp, verbose);

	while(args.length() > 0)
	{
		if((current = args.find('\n')) != std::string::npos)
		{
			arg = args.substr(0, current);
			args.erase(0, current + 1);
		}
		else
		{
			arg = args;
			args.clear();
		}

		for(attempt = 0; attempt < 3; attempt++)
		{
			if(!send_socket.send(arg, GenericSocket::cooked))
			{
				if(verbose)
					std::cout << "send failed, attempt #" << attempt << std::endl;

				goto retry;
			}

			if(!send_socket.receive(reply, GenericSocket::cooked, flash_sector_size))
			{
				if(verbose)
					std::cout << "receive failed, attempt #" << attempt << std::endl;

				goto retry;
			}

			attempt = 0;
			break;

	retry:
			send_socket.drain();
		}

		if(attempt != 0)
			throw(std::string("send/receive failed"));

		std::cout << reply << std::endl;
	}
}

void command_multicast(const std::string &host, const std::string &port, bool verbose, int multicast_repeats, bool dontwait, const std::string &args)
{
	std::string reply;
	GenericSocket multicast_socket(host, port, true, verbose, multicast_repeats);

	if(!multicast_socket.send(args, GenericSocket::cooked))
		throw(std::string("mulicast send failed"));

	if(!dontwait)
	{
		if(!multicast_socket.receive(reply, GenericSocket::cooked, flash_sector_size))
			throw(std::string("multicast receive failed"));

		std::cout << reply << std::endl;
	}
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
	command_channel.send(std::string("reset"), GenericSocket::cooked);
	usleep(200000);
	command_channel.drain();
	command_channel.disconnect();
	usleep(1000000);
	command_channel.connect();
	std::cout << "reboot finished" << std::endl;

	process(command_channel, "mailbox-info", reply, mailbox_info_reply, string_value, int_value, verbose);

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

	process(command_channel, "stats", reply, "\\s*>\\s*firmware\\s*>\\s*date:\\s*([a-zA-Z0-9: ]+).*", string_value, int_value, verbose);
	std::cout << "firmware version: " << string_value[0] << std::endl;
}

int main(int argc, const char **argv)
{
	po::options_description	options("usage");

	try
	{
		std::vector<std::string> host_args;
		std::string host;
		std::string args;
		std::string command_port;
		std::string mailbox_port;
		std::string filename;
		std::string start_string;
		std::string length_string;
		int start;
		int image_slot;
		int image_timeout;
		int dim_x, dim_y, depth;
		unsigned int length;
		unsigned int chunk_size;
		bool use_udp = false;
		bool dont_wait = false;
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
		bool cmd_image = false;
		bool cmd_image_epaper = false;
		int cmd_multicast = 0;
		bool cmd_read = false;
		bool cmd_info = false;
		unsigned int selected;

		options.add_options()
			("info,i",			po::bool_switch(&cmd_info)->implicit_value(true),					"INFO")
			("read,R",			po::bool_switch(&cmd_read)->implicit_value(true),					"READ")
			("checksum,C",		po::bool_switch(&cmd_checksum)->implicit_value(true),				"CHECKSUM")
			("verify,V",		po::bool_switch(&cmd_verify)->implicit_value(true),					"VERIFY")
			("simulate,S",		po::bool_switch(&cmd_simulate)->implicit_value(true),				"WRITE simulate")
			("write,W",			po::bool_switch(&cmd_write)->implicit_value(true),					"WRITE")
			("benchmark,B",		po::bool_switch(&cmd_benchmark)->implicit_value(true),				"BENCHMARK")
			("image,I",			po::bool_switch(&cmd_image)->implicit_value(true),					"SEND IMAGE")
			("epaper-image,e",	po::bool_switch(&cmd_image_epaper)->implicit_value(true),			"SEND EPAPER IMAGE (uc8151d connected to host)")
			("multicast,M",		po::value<int>(&cmd_multicast)->implicit_value(3),					"MULTICAST SENDER send multicast message (arg is repeat count)")
			("host,h",			po::value<std::vector<std::string> >(&host_args)->required(),		"host or multicast group to use")
			("verbose,v",		po::bool_switch(&verbose)->implicit_value(true),					"verbose output")
			("udp,u",			po::bool_switch(&use_udp)->implicit_value(true),					"use UDP instead of TCP")
			("filename,f",		po::value<std::string>(&filename),									"file name")
			("start,s",			po::value<std::string>(&start_string)->default_value("-1"),			"send/receive start address (OTA is default)")
			("length,l",		po::value<std::string>(&length_string)->default_value("0x1000"),	"read length")
			("command-port,p",	po::value<std::string>(&command_port)->default_value("24"),			"command port to connect to")
			("mailbox-port,P",	po::value<std::string>(&mailbox_port)->default_value("26"),			"mailbox port to connect to")
			("nocommit,n",		po::bool_switch(&nocommit)->implicit_value(true),					"don't commit after writing")
			("noreset,N",		po::bool_switch(&noreset)->implicit_value(true),					"don't reset after commit")
			("notemp,t",		po::bool_switch(&notemp)->implicit_value(true),						"don't commit temporarily, commit to flash")
			("dontwait,d",		po::bool_switch(&dont_wait)->implicit_value(true),					"don't wait for reply on message")
			("image_slot,x",	po::value<int>(&image_slot)->default_value(-1),						"send image to flash slot x instead of frame buffer")
			("image_timeout,y",	po::value<int>(&image_timeout)->default_value(5000),				"freeze frame buffer for y ms after sending");

		po::positional_options_description positional_options;
		positional_options.add("host", -1);

		po::variables_map varmap;
		auto parsed = po::command_line_parser(argc, argv).options(options).positional(positional_options).run();
		po::store(parsed, varmap);
		po::notify(varmap);

		auto it = host_args.begin();
		host = *(it++);
		auto it1 = it;

		for(; it != host_args.end(); it++)
		{
			if(it != it1)
				args.append(" ");

			args.append(*it);
		}

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

		if(cmd_image)
			selected++;

		if(cmd_image_epaper)
			selected++;

		if(cmd_read)
			selected++;

		if(cmd_info)
			selected++;

		if(cmd_multicast > 0)
			selected++;

		if(selected > 1)
			throw(std::string("specify one of write/simulate/verify/checksum/image/epaper-image/read/info"));

		if(selected == 0)
			command_send(host, command_port, use_udp, verbose, dont_wait, args);
		else
		{
			if(cmd_multicast > 0)
				command_multicast(host, command_port, verbose, cmd_multicast, dont_wait, args);
			else
			{
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
				GenericSocket mailbox_channel(host, mailbox_port, true, verbose);
				mailbox_channel.send(std::string(" "), GenericSocket::raw);

				try
				{
					process(command_channel, "mailbox-info", reply, mailbox_info_reply, string_value, int_value, verbose);
				}
				catch(std::string &e)
				{
					throw(std::string("MAILBOX incompatible image: ") + e);
				}

				flash_slot = int_value[0];
				flash_address[0] = int_value[1];
				flash_address[1] = int_value[2];
				dim_x = int_value[3];
				dim_y = int_value[4];
				depth = int_value[5];

				std::cout << "MAILBOX update available, current slot: " << flash_slot;
				std::cout << ", address[0]: 0x" << std::hex << (flash_address[0] * flash_sector_size) << " (sector " << std::dec << flash_address[0] << ")";
				std::cout << ", address[1]: 0x" << std::hex << (flash_address[1] * flash_sector_size) << " (sector " << std::dec << flash_address[1] << ")";
				std::cout << ", display graphical dimensions: " << dim_x << "x" << dim_y << " px @" << depth;
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
						if(!cmd_benchmark && !cmd_image && !cmd_image_epaper)
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
									else
										if(cmd_image)
											command_image(command_channel, mailbox_channel, image_slot, filename, chunk_size, dim_x, dim_y, depth, image_timeout, verbose);
										else
											if(cmd_image_epaper)
												command_image_epaper(command_channel, filename, verbose);
			}
		}
	}
	catch(const po::error &e)
	{
		std::cerr << std::endl << "espif: " << e.what() << std::endl << options;
		return(1);
	}
	catch(const std::exception &e)
	{
		std::cerr << std::endl << "espif: " << e.what() << std::endl;
		return(1);
	}
	catch(const std::string &e)
	{
		std::cerr << std::endl << "espif: " << e << std::endl;
		return(1);
	}
	catch(...)
	{
		std::cerr << std::endl << "espif: unknown exception caught" << std::endl;
		return(1);
	}

	return(0);
}
