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

		int fd;
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
		void disconnect();
		void connect();
};

GenericSocket::GenericSocket(const std::string &host_in, const std::string &service_in, bool use_udp_in, bool verbose_in, int multicast_in)
		: fd(-1), service(service_in), use_udp(use_udp_in), verbose(verbose_in), multicast(multicast_in)
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

	if((fd = socket(AF_INET, (use_udp || (multicast > 0)) ? SOCK_DGRAM : SOCK_STREAM, 0)) < 0)
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

		if(setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &arg, sizeof(arg)))
			throw(std::string("multicast: cannot set mc ttl"));

		arg = 0;

		if(setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, &arg, sizeof(arg)))
			throw(std::string("multicast: cannot set loopback"));

		mreq.imr_multiaddr = saddr.sin_addr;
		mreq.imr_interface.s_addr = INADDR_ANY;

		if(setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)))
			throw(std::string("multicast: cannot join mc group"));
	}
	else
	{
		if(::connect(fd, (const struct sockaddr *)&saddr, sizeof(saddr)))
			throw(std::string("connect failed"));
	}
}

void GenericSocket::disconnect()
{
	if(fd >= 0)
		close(fd);

	fd = -1;
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
			write(fd, buffer.data(), 0);
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
			if(sendto(fd, buffer.data(), chunk, MSG_DONTWAIT, (const sockaddr *)&saddr, sizeof(saddr)) != chunk)
				return(false);

			usleep(200000);
		}

		return(true);
	}

	while(buffer.length() > 0)
	{
		pfd.fd = fd;
		pfd.events = POLLOUT | POLLERR | POLLHUP;
		pfd.revents = 0;

		if(poll(&pfd, 1, 2000) != 1)
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

bool GenericSocket::receive(std::string &reply, GenericSocket::process_t how, int expected)
{
	int attempt;
	int length;
	struct pollfd pfd = { .fd = fd, .events = POLLIN | POLLERR | POLLHUP, .revents = 0 };
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
			if((length = recvfrom(fd, buffer, sizeof(buffer), 0, (struct sockaddr *)&remote_addr, &remote_length)) < 0)
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
			if(poll(&pfd, 1, 2000) != 1)
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

			if((length = read(fd, buffer, sizeof(buffer) - 1)) < 0)
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

		send_status = channel.send(send_string, GenericSocket::cooked);

		if(send_status)
			receive_status = channel.receive(reply_string, GenericSocket::cooked);
		else
			receive_status = false;

		if(reply_string.length() == 0)
			receive_status = false;

		if(send_status && receive_status)
			break;

		std::cout << std::endl << (!send_status ? "send" : "receive") << " failed, retry #" << (max_attempts - attempt) << std::endl;

		channel.disconnect();
		usleep(500000);
		channel.connect();
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

					if(!mailbox_channel.send(std::string((const char *)sector_buffer, flash_sector_size), GenericSocket::raw))
						throw(std::string("send failed"));

					if(!mailbox_channel.receive(reply, GenericSocket::raw, ack_string.length()))
						throw(std::string("receive failed"));

					if(verbose)
						std::cout << "mailbox replied: \"" << reply << "\"" << std::endl;


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

					mailbox_channel.disconnect();
					usleep(100000);
					mailbox_channel.connect();
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
			}

			if(sector_attempt <= 0)
				throw(std::string("! receiving sector failed too many times"));

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

					if((file_offset = lseek(fd, (current - start) * flash_sector_size, SEEK_SET)) < 0)
						throw(std::string("i/o error in seek"));

					memset(sector_buffer, 0xff, sizeof(sector_buffer));

					if(read(fd, sector_buffer, sizeof(sector_buffer)) <= 0)
						throw(std::string("i/o error in read"));

					if(memcmp(reply.data(), sector_buffer, sizeof(sector_buffer)))
					{
						if(verbose)
							std::cout << std::endl << "! data mismatch" << std::endl;
						goto error;
					}

					break;
				}
error:
				if(verbose)
				{
					std::cout << "! receive failed, sector #" << current << ", #" << (current - start) << ", attempt #" << max_attempts - sector_attempt;
					std::cout << std::endl;
				}
			}

			if(sector_attempt == 0)
				throw(std::string("! receiving sector failed too many times"));

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
	static const char *ack = "ACK";
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

		if(!mailbox_channel.send(std::string((const char *)sector_buffer, flash_sector_size), GenericSocket::raw))
			throw(std::string("send failed"));

		if(!mailbox_channel.receive(reply, GenericSocket::cooked, strlen(ack)))
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
			process(command_channel, send_string, reply, "OK mailbox-bench: sending one sector", string_value, int_value, verbose);
		}
		catch(const std::string &e)
		{
			goto error;
		}

		reply.clear();

		if(!mailbox_channel.receive(reply, GenericSocket::raw, flash_sector_size))
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

		mailbox_channel.disconnect();
		mailbox_channel.connect();
		mailbox_channel.send(std::string(" "), GenericSocket::raw);

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

static void command_image_send_sector(GenericSocket &command_channel, GenericSocket &mailbox_channel, const unsigned char *buffer, unsigned int length,
		unsigned int current_x, unsigned int current_y, bool verbose)
{
	enum { attempts =  8 };
	unsigned int attempt;
	std::vector<int> int_value;
	std::vector<std::string> string_value;
	std::string reply;

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

			if(!mailbox_channel.receive(reply, GenericSocket::raw, ack_string.length()))
			{
				if(verbose)
					std::cout << "receive ack failed, attempt: " << attempt << std::endl;

				goto error;
			}

			process(command_channel, std::string("display-canvas-plot ") +
					std::to_string(length / 2) + " " +
					std::to_string(current_x) + " " +
					std::to_string(current_y),
					reply,
					"display canvas plot success: yes",
					string_value, int_value, verbose);
		}
		catch(const std::string &e)
		{
			if(verbose)
				std::cout << "send failed, attempt: " << attempt << ", reason: " << e << std::endl;

			goto error;
		}

		break;

error:
		process(command_channel, "mailbox-reset", reply, "OK mailbox-reset", string_value, int_value, verbose);
	}

	if(attempt >= attempts)
		throw(std::string("send failed, max attempts reached"));
}

static void command_image(GenericSocket &command_channel, GenericSocket &mailbox_channel, const std::string &filename, unsigned chunk_size,
		unsigned int dim_x, unsigned int dim_y, unsigned int depth, bool verbose)
{
	std::vector<int> int_value;
	std::vector<std::string> string_value;
	std::string reply;
	unsigned char sector_buffer[flash_sector_size];
	unsigned int start_x, start_y;
	unsigned int current_buffer, x, y, r, g, b, r1, g1, g2, b1;

	try
	{
		Magick::Image image;
		Magick::Geometry newsize(dim_x, dim_y);
		Magick::Color colour;
		newsize.aspect(true);

		image.read(filename);

		if(verbose)
			std::cout << "image loaded from " << filename << ", " << image.columns() << "x" << image.rows() << ", " << image.magick() << std::endl;

		image.resize(newsize);

		if((image.columns() != dim_x) || (image.rows() != dim_y))
			throw(std::string("image magic resize failed"));

		current_buffer = 0;
		start_x = 0;
		start_y = 0;

		process(command_channel, "display-canvas-start 20000", reply, "display canvas start success: yes", string_value, int_value, verbose);

		if(depth == 12)
		{
			for(y = 0; y < dim_y; y++)
			{
				for(x = 0; x < dim_x; x++)
				{
					colour = image.pixelColor(x, y);
					r = colour.redQuantum() >> 11;
					g = colour.greenQuantum() >> 10;
					b = colour.blueQuantum() >> 11;

					if((current_buffer + 2) > chunk_size)
					{
						command_image_send_sector(command_channel, mailbox_channel, sector_buffer, current_buffer, start_x, start_y, verbose);
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
				}
			}

			if(current_buffer > 0)
				command_image_send_sector(command_channel, mailbox_channel, sector_buffer, current_buffer, start_x, start_y, verbose);
		}

		process(command_channel, "display-canvas-show", reply, "display canvas show success: yes", string_value, int_value, verbose);
		process(command_channel, "display-canvas-stop", reply, "display canvas stop success: yes", string_value, int_value, verbose);
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

void command_connect(const std::string &host, const std::string &port, bool udp, bool verbose, const std::string &args)
{
	std::string reply;
	unsigned int attempt;
	GenericSocket connect_socket(host, port, udp, verbose);

	for(attempt = 0; attempt < 3; attempt++)
	{
		if(!connect_socket.send(args, GenericSocket::cooked))
		{
			if(verbose)
				std::cout << "send failed, attempt #" << attempt << std::endl;

			goto retry;
		}

		if(!connect_socket.receive(reply, GenericSocket::cooked, flash_sector_size))
		{
			if(verbose)
				std::cout << "receive failed, attempt #" << attempt << std::endl;

			goto retry;
		}

		attempt = 0;
		break;

retry:
		connect_socket.disconnect();
		usleep(200000);
		connect_socket.connect();
	}

	if(attempt != 0)
		throw(std::string("send/receive failed"));

	std::cout << reply << std::endl;
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
			("dontwait,d",		po::bool_switch(&dont_wait)->implicit_value(true),					"don't wait for reply on multicast message");

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

		if(cmd_read)
			selected++;

		if(cmd_info)
			selected++;

		if(cmd_multicast > 0)
			selected++;

		if(selected > 1)
			throw(std::string("specify one of write/simulate/verify/checksum/image/read/info"));

		if(selected == 0)
			command_connect(host, command_port, use_udp, verbose, args);
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
						if(!cmd_benchmark && !cmd_image)
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
											command_image(command_channel, mailbox_channel, filename, chunk_size, dim_x, dim_y, depth, verbose);
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
