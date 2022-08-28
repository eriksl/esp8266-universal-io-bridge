#include "espif.h"

extern "C" {
#define __espif__
#define assert_size(type, size) static_assert(sizeof(type) == size, "sizeof(" #type ") != " #size)
#define attr_packed __attribute__ ((__packed__))
#define _Static_assert static_assert
#include "ota.h"
#undef assert_size
#undef attr_packed
}

namespace po = boost::program_options;

enum
{
	max_attempts = 16,
	max_udp_packet_size = 1472,
	flash_sector_size = 4096,
	md5_hash_size = 16,
	sha1_hash_size = 20,
};

typedef std::vector<std::string> StringVector;

static const std::string flash_info_expect("OK flash function available, slots: 2, current: ([0-9]+), sectors: \\[ ([0-9]+), ([0-9]+) \\](?:, display: ([0-9]+)x([0-9]+)px@([0-9]+))?.*");
static const std::string flash_read_expect("OK flash-read: read sector ([0-9]+)\n.*");
static const std::string flash_write_expect("OK flash-write: written mode ([01]), sector ([0-9]+), same ([01]), erased ([01])\n");
static const std::string flash_checksum_expect("OK flash-checksum: checksummed ([0-9]+) sectors from sector ([0-9]+), checksum: ([0-9a-f]+)\n");
static const std::string flash_select_expect("OK flash-select: slot ([0-9]+) selected, sector ([0-9]+), permanent ([0-1])\n");

static uint32_t MD5_trunc_32(unsigned int length, const uint8_t *data)
{
	uint8_t hash[md5_hash_size];
	uint32_t checksum;
	unsigned int hash_size;
	EVP_MD_CTX *hash_ctx;

	hash_ctx = EVP_MD_CTX_new();
	EVP_DigestInit_ex(hash_ctx, EVP_md5(), (ENGINE *)0);
	EVP_DigestUpdate(hash_ctx, data, length);
	hash_size = md5_hash_size;
	EVP_DigestFinal_ex(hash_ctx, hash, &hash_size);
	EVP_MD_CTX_free(hash_ctx);

	checksum = (hash[0] << 24) | (hash[1] << 16) | (hash[2] << 8) | (hash[3] << 0);

	return(checksum);
}

class GenericSocket
{
	private:

		int socket_fd;
		std::string host;
		std::string service;
		bool use_udp, verbose, broadcast, multicast;
		struct sockaddr_in saddr;
		bool no_provide_checksum;
		bool no_request_checksum;
		bool raw;
		int broadcast_group;

		void send_prepare(const std::string &src, std::string &dst);

	public:
		GenericSocket(const std::string &host, const std::string &port, bool use_udp, bool verbose,
				bool broadcast, bool multicast,
				bool no_provide_checksum, bool no_request_checksum, bool raw, int broadcast_group);
		~GenericSocket();

		bool send_unicast(const std::string &buffer);
		bool send_multicast(std::string buffer);
		bool receive_unicast(std::string &buffer);
		bool receive_multicast(std::string &buffer);
		void drain();
		void disconnect();
		void connect();
};

GenericSocket::GenericSocket(const std::string &host_in, const std::string &service_in, bool use_udp_in, bool verbose_in,
		bool broadcast_in, bool multicast_in,
		bool no_provide_checksum_in, bool no_request_checksum_in, bool raw_in, int broadcast_group_in)
	: socket_fd(-1), service(service_in), use_udp(use_udp_in), verbose(verbose_in),
		broadcast(broadcast_in), multicast(multicast_in),
		no_provide_checksum(no_provide_checksum_in), no_request_checksum(no_request_checksum_in),
		raw(raw_in), broadcast_group(broadcast_group_in)
{
	memset(&saddr, 0, sizeof(saddr));

	if(multicast)
		host = std::string("239.255.255.") + host_in;
	else
		host = host_in;

	if(multicast || broadcast)
		use_udp = true;

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

	if((socket_fd = socket(AF_INET, use_udp ? SOCK_DGRAM : SOCK_STREAM, 0)) < 0)
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

	if(broadcast)
	{
		int arg;

		arg = 1;

		if(setsockopt(socket_fd, SOL_SOCKET, SO_BROADCAST, &arg, sizeof(arg)))
		{
			if(verbose)
				perror("setsockopt SO_BROADCAST\n");
			throw(std::string("set broadcast"));
		}
	}

	if(multicast)
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
		if(!use_udp && ::connect(socket_fd, (const struct sockaddr *)&saddr, sizeof(saddr)))
			throw(std::string("connect failed"));
}

void GenericSocket::disconnect()
{
	if(socket_fd >= 0)
		close(socket_fd);

	socket_fd = -1;
}

void GenericSocket::send_prepare(const std::string &src, std::string &dst)
{
	dst = src;

	if(raw)
		dst.append("\n");
	else
	{
		packet_header_t packet_header;

		packet_header.id = packet_header_id;
		packet_header.length = src.length() + sizeof(packet_header);
		packet_header.checksum = packet_header_checksum_dummy;
		packet_header.flags = 0;

		if(!no_request_checksum)
			packet_header.flags |= packet_header_flags_md5_32_requested;

		if((broadcast_group >= 0) && (broadcast_group < 8))
		{
			packet_header.flags |= packet_header_flags_use_bc_group;
			packet_header.flags |= (1 << broadcast_group) << packet_header_flag_bc_group_shift;
		}

		if(!no_provide_checksum)
		{
			packet_header.flags |= packet_header_flags_md5_32_provided;
			std::string buffer_packet_checksum = src;
			buffer_packet_checksum.insert(0, (const char *)&packet_header, sizeof(packet_header));
			packet_header.checksum = MD5_trunc_32(buffer_packet_checksum.length(), (const uint8_t *)buffer_packet_checksum.data());
		}

		dst.insert(0, (const char *)&packet_header, sizeof(packet_header));
	}
}

bool GenericSocket::send_unicast(const std::string &text)
{
	struct pollfd pfd;
	int chunk;
	std::string data;

	send_prepare(text, data);

	while(data.length() > 0)
	{
		pfd.fd = socket_fd;
		pfd.events = POLLOUT | POLLERR | POLLHUP;
		pfd.revents = 0;

		if(poll(&pfd, 1, 500) != 1)
			return(false);

		if(pfd.revents & (POLLERR | POLLHUP))
			return(false);

		chunk = data.length();

		if(use_udp)
		{
			if(::sendto(socket_fd, data.data(), chunk, 0, (const struct sockaddr *)&this->saddr, sizeof(this->saddr)) != chunk)
				return(false);
		}
		else
			if(::send(socket_fd, data.data(), chunk, 0) != chunk)
				return(false);

		data.erase(0, chunk);
	}

	return(true);
}

bool GenericSocket::send_multicast(std::string text)
{
	int run;
	std::string data;

	send_prepare(text, data);

	if(data.length() > max_udp_packet_size)
		throw(std::string("multicast packet > max udp size"));

	for(run = 0; run < 8; run++)
	{
		if(::sendto(socket_fd, data.data(), data.length(), 0, (const struct sockaddr *)&this->saddr, sizeof(this->saddr)) != (int)data.length())
			return(false);

		usleep(50000);
	}

	return(true);
}

bool GenericSocket::receive_unicast(std::string &reply)
{
	int length;
	int packet_length;
	unsigned int fragment;
	struct pollfd pfd = { .fd = socket_fd, .events = POLLIN | POLLERR | POLLHUP, .revents = 0 };
	char buffer[flash_sector_size + ota_data_offset + sizeof(packet_header_t) + 16];
	enum { max_fragments = 32 };
	uint32_t our_checksum, their_checksum;

	packet_length = -1;
	reply.clear();

	for(fragment = 0; fragment < max_fragments; fragment++)
	{
		if(poll(&pfd, 1, 500) != 1)
		{
			if(verbose)
				std::cout << boost::format("receive: timeout, fragment: %u, length: %u\n") % fragment % reply.length();
			return(false);
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

		if(use_udp)
		{
			if((length = ::recvfrom(socket_fd, buffer, sizeof(buffer) - 1, 0, (struct sockaddr *)0, 0)) < 0)
			{
				if(verbose)
					std::cout << "udp receive: length < 0" << std::endl;
				return(false);
			}
		}
		else
		{
			if((length = ::recv(socket_fd, buffer, sizeof(buffer) - 1, 0)) < 0)
			{
				if(verbose)
					std::cout << "tcp receive: length < 0" << std::endl;
				return(false);
			}
		}

		reply.append(buffer, (size_t)length);

		if(raw)
		{
			if(reply.back() == '\n')
				break;
		}
		else
		{
			if((packet_length < 0) && (reply.length() >= sizeof(packet_header_t)))
			{
				const packet_header_t *packet_header = (const packet_header_t *)reply.data();

				if(packet_header->id == packet_header_id)
				{
					packet_length = packet_header->length;

					if(packet_length >= (int)(sizeof(packet_header_t) + ota_data_offset + flash_sector_size + 16))
					{
						if(verbose)
							std::cout << "receive: invalid packet length" << std::endl;
						return(false);
					}
				}
			}
		}

		if((int)reply.length() >= packet_length)
			break;
	}

	if(!raw)
	{
		if(reply.length() < sizeof(packet_header_t))
			return(false);

		packet_header_t *packet_header;

		std::string packet_header_data = reply.substr(0, sizeof(packet_header_t));
		packet_header = (packet_header_t *)packet_header_data.data();
		reply.erase(0, sizeof(packet_header_t));

		if(packet_header->flags & packet_header_flags_md5_32_provided)
		{
			std::string checksum_buffer;

			their_checksum = packet_header->checksum;
			packet_header->checksum = packet_header_checksum_dummy;
			checksum_buffer = packet_header_data + reply;
			our_checksum = MD5_trunc_32(checksum_buffer.length(), (const uint8_t *)checksum_buffer.data());

			if(our_checksum != their_checksum)
			{
				if(verbose)
					std::cout << boost::format("CRC mismatch, our CRC: %08x, their CRC: %08x\n") % our_checksum % their_checksum;

				return(false);
			}
		}
	}

	return(true);
}

bool GenericSocket::receive_multicast(std::string &reply)
{
	int length;
	unsigned int attempt;
	struct pollfd pfd = { .fd = socket_fd, .events = POLLIN | POLLERR | POLLHUP, .revents = 0 };
	char buffer[flash_sector_size + ota_data_offset + 16];
	enum { max_attempts = 32 };
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

	reply.clear();

	total_replies = total_hosts = 0;

	gettimeofday(&tv_start, nullptr);
	start = (tv_start.tv_sec * 1000000) + tv_start.tv_usec;

	for(attempt = 0; attempt < max_attempts; attempt++)
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
		if((length = ::recvfrom(socket_fd, buffer, sizeof(buffer), 0, (struct sockaddr *)&remote_addr, &remote_length)) < 0)
			return(false);

		attempt = 0;

		if(length == 0)
			continue;

		buffer[length] = '\0';

		if(!raw)
		{
			if((length >= (int)sizeof(packet_header_t)))
			{
				packet_header_t *packet_header = (packet_header_t *)buffer;
				line = "";

				if(packet_header->flags & packet_header_flags_md5_32_provided)
				{
					unsigned int their_checksum;

					their_checksum = packet_header->checksum;
					packet_header->checksum = packet_header_checksum_dummy;

					if(their_checksum != MD5_trunc_32(length, (const uint8_t *)buffer))
						line = " <checksum invalid>";
				}

				line = std::string(&buffer[sizeof(packet_header_t)]) + line;
			}
			else
				line = std::string(buffer) + " <missing packet header>";
		}

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
		text << " " << std::setw(2) << std::right << it.second.count << " ";
		text << std::setw(12) << std::left << it.second.hostname;
		text << " " << it.second.text;
	}

	text << std::endl << "Total of " << total_replies << " replies received, " << total_hosts << " hosts" << std::endl;

	reply = text.str();

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

		if(poll(&pfd, 1, 500) != 1)
			return;

		if(pfd.revents & (POLLERR | POLLHUP))
			return;

		if(use_udp)
		{
			if(::recvfrom(socket_fd, buffer, sizeof(buffer), 0, (struct sockaddr *)0, 0) < 0)
				return;
		}
		else
		{
			if(::recv(socket_fd, buffer, sizeof(buffer), 0) < 0)
				return;
		}
	}
}

static std::string sha_hash_to_text(const unsigned char *hash)
{
	unsigned int current;
	std::stringstream hash_string;

	for(current = 0; current < sha1_hash_size; current++)
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

	reply_string.clear();

	if(verbose)
	{
		int length = 0;

		std::cout << "> send (" << send_string.length() << "): \"";

		for(const auto &it : send_string)
		{
			if(length++ > 80)
				break;

			if((it < ' ') || (it > '~'))
				std::cout << '.';
			else
				std::cout << it;
		}

		std::cout << "\"" << std::endl;
	}

	for(attempt = 0; attempt < 3; attempt++)
	{
		if(!channel.send_unicast(send_string))
		{
			if(verbose)
				std::cout << "process: send failed #" << attempt << std::endl;

			channel.drain();
			continue;
		}

		if(!channel.receive_unicast(reply_string))
		{
			if(verbose)
				std::cout << "process: receive failed #" << attempt << std::endl;

			channel.drain();
			continue;
		}

		if(reply_string.length() == 0)
		{
			if(verbose)
				std::cout << std::endl << "process: empty string received" << std::endl;

			continue;
		}

		break;
	}

	if(attempt >= 3)
		throw(std::string("process: receive failed"));

	if(verbose)
		std::cout << "< receive (" << reply_string.length() << "): \"" << reply_string.substr(0, 160) << "\"" << std::endl;

	if(!boost::regex_match(reply_string, capture, re))
		throw(std::string("received string does not match"));

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

static void read_sector(GenericSocket &command_channel, unsigned int sector, std::string &data, bool verbose)
{
	unsigned int attempt;
	std::string reply;
	std::vector<int> int_value;
	std::vector<std::string> string_value;

	for(attempt = 0; attempt < max_attempts; attempt++)
	{
		try
		{
			process(command_channel, std::string("flash-read ") + std::to_string(sector) + "\n", reply, flash_read_expect, string_value, int_value,
					verbose);
		}
		catch(const std::string &error)
		{
			if(verbose)
				std::cout << "flash sector read failed: " << error << ", attempt " << std::to_string(attempt) << ", reply: " << reply.substr(0, 80) << std::endl;

			command_channel.drain();
			continue;
		}

		if(reply.length() < (flash_sector_size + ota_data_offset))
		{
			if(verbose)
			{
				std::cout << "flash sector read failed: incorrect length, attempt " << std::to_string(attempt);
				std::cout << ", expected: " << (flash_sector_size + ota_data_offset) << ", received: " << reply.length();
				std::cout << ", reply: " << reply.substr(0, 80) << std::endl;
			}

			command_channel.drain();
			continue;
		}

		if(int_value[0] != (int)sector)
		{
			if(verbose)
				std::cout << "flash sector read failed: local sector (" << sector + ") != remote sector (" << int_value[0] << ")" << std::endl;

			command_channel.drain();
			continue;
		}

		data = reply.substr(ota_data_offset, flash_sector_size);
		break;
	}

	if(attempt >= max_attempts)
		throw(std::string("flash sector read failed"));
}

static void write_sector(GenericSocket &command_channel, unsigned int sector, const uint8_t *data,
		unsigned int &written, unsigned int &erased, unsigned int &skipped, bool simulate, bool verbose)
{
	unsigned int attempt;
	unsigned int length;
	std::string command;
	std::string reply;
	std::vector<int> int_value;
	std::vector<std::string> string_value;

	command = (boost::format("flash-write %u %u") % (simulate ? 0 : 1) % sector).str();

	length = command.length();

	if(length >= ota_data_offset)
		throw(std::string("command > ota_data_offset"));

	command.append(ota_data_offset - length, ' ');
	command.append((const char *)data, flash_sector_size);

	for(attempt = 0; attempt < max_attempts; attempt++)
	{
		try
		{
			process(command_channel, command, reply, flash_write_expect, string_value, int_value, verbose);
		}
		catch(const std::string &error)
		{
			if(verbose)
				std::cout << "flash sector write failed: " << error << ", attempt " << std::to_string(attempt) << ", reply: " << reply.substr(0, 80) << std::endl;

			command_channel.drain();
			continue;
		}

		if(int_value[0] != (simulate ? 0 : 1))
		{
			if(verbose)
				std::cout << boost::format("flash sector write failed: mode local: %u != mode remote %u\n") % (simulate ? 0 : 1) % int_value[0];

			command_channel.drain();
			continue;
		}

		if(int_value[1] != (int)sector)
		{
			if(verbose)
				std::cout << boost::format("flash sector write failed: sector local: %u != sector remote %u\n") % sector % int_value[0];

			command_channel.drain();
			continue;
		}

		if(int_value[2] != 0)
			skipped++;
		else
			written++;

		if(int_value[3] != 0)
			erased++;

		break;
	}

	if(attempt >= max_attempts)
		throw(std::string("flash sector write failed"));
}

static void get_checksum(GenericSocket &command_channel, unsigned int sector, unsigned int sectors, std::string &checksum, bool verbose)
{
	unsigned int attempt;
	std::string reply;
	std::vector<int> int_value;
	std::vector<std::string> string_value;

	for(attempt = 0; attempt < max_attempts; attempt++)
	{
		try
		{
			process(command_channel, std::string("flash-checksum ") + std::to_string(sector) + " " + std::to_string(sectors) + "\n",
					reply, flash_checksum_expect, string_value, int_value, verbose);
		}
		catch(const std::string &error)
		{
			if(verbose)
				std::cout << "flash sector checksum failed: " << error << ", attempt " << std::to_string(attempt) << ", reply: " << reply << std::endl;

			continue;
		}

		if(int_value[0] != (int)sectors)
		{
			if(verbose)
				std::cout << "flash sector checksum failed: local sectors (" << sectors + ") != remote sectors (" << int_value[0] << ")" << std::endl;

			continue;
		}

		if(int_value[1] != (int)sector)
		{
			if(verbose)
				std::cout << "flash sector checksum failed: local start sector (" << sector << ") != remote start sector (" << int_value[1] << ")" << std::endl;

			continue;
		}

		checksum = string_value[2];

		break;
	}

	if(attempt >= max_attempts)
		throw(std::string("flash sector verify failed"));
}

static void command_read(GenericSocket &command_channel, const std::string &filename, int sector, int sectors, bool verbose)
{
	int64_t file_offset;
	int file_fd;
	int current;
	struct timeval time_start, time_now;
	std::string send_string;
	std::string operation;
	std::vector<int> int_value;
	std::vector<std::string> string_value;
	EVP_MD_CTX *hash_ctx;
	unsigned int hash_size;
	unsigned char hash[sha1_hash_size];
	std::string sha_local_hash_text;
	std::string sha_remote_hash_text;
	std::string data;

	if(filename.empty())
		throw(std::string("file name required"));

	if((file_fd = open(filename.c_str(), O_WRONLY | O_TRUNC | O_CREAT, 0666)) < 0)
		throw(std::string("can't create file"));

	try
	{
		gettimeofday(&time_start, 0);

		std::cout << "start read from 0x" << std::hex << sector * flash_sector_size << " (" << std::dec << sector << ")";
		std::cout << ", length: 0x" << std::hex << sectors * flash_sector_size << " (" << std::dec << sectors << ")";
		std::cout << std::endl;

		hash_ctx = EVP_MD_CTX_new();
		EVP_DigestInit_ex(hash_ctx, EVP_sha1(), (ENGINE *)0);

		for(current = sector; current < (sector + sectors); current++)
		{
			read_sector(command_channel, current, data, verbose);

			if((file_offset = lseek(file_fd, 0, SEEK_CUR)) < 0)
				throw(std::string("i/o error in seek"));

			if(write(file_fd, data.data(), data.length()) <= 0)
				throw(std::string("i/o error in write"));

			EVP_DigestUpdate(hash_ctx, (const unsigned char *)data.data(), data.length());

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
				std::cout << ", received "	<< std::setw(2) << (current - sector) << " sectors";
				std::cout << ", "			<< std::setw(3) << (((file_offset + flash_sector_size) * 100) / (sectors * flash_sector_size)) << "%       \r";
				std::cout.flush();
			}
		}
	}
	catch(const std::string &e)
	{
		if(!verbose)
			std::cout << std::endl;

		close(file_fd);
		throw;
	}

	close(file_fd);

	if(!verbose)
		std::cout << std::endl;

	std::cout << "checksumming " << sectors << " sectors from " << sector << "..." << std::endl;

	hash_size = sha1_hash_size;
	EVP_DigestFinal_ex(hash_ctx, hash, &hash_size);
	EVP_MD_CTX_free(hash_ctx);

	sha_local_hash_text = sha_hash_to_text(hash);
	get_checksum(command_channel, sector, sectors, sha_remote_hash_text, verbose);

	if(sha_local_hash_text != sha_remote_hash_text)
	{
		if(verbose)
		{
			std::cout << "! sector " << sector << "/" << sectors;
			std::cout << ", address: 0x" << std::hex << (sector * flash_sector_size) << "/0x" << (sectors * flash_sector_size) << std::dec << " read";
			std::cout << ", checksum failed";
			std::cout << ", local hash: " << sha_local_hash_text;
			std::cout << ", remote hash: " << sha_remote_hash_text;
			std::cout << std::endl;
		}

		throw(std::string("checksum read failed"));
	}

	std::cout << "checksum OK" << std::endl;
}

void command_write(GenericSocket &command_channel, const std::string filename, int sector, bool verbose, bool simulate, bool otawrite)
{
	int64_t file_offset;
	int file_fd;
	int length;
	int current;
	struct timeval time_start, time_now;
	std::string send_string;
	std::string reply;
	std::vector<int> int_value;
	std::vector<std::string> string_value;
	EVP_MD_CTX *hash_ctx;
	unsigned int hash_size;
	unsigned char hash[sha1_hash_size];
	std::string sha_local_hash_text;
	std::string sha_remote_hash_text;
	std::string data;
	unsigned int sectors_written, sectors_skipped, sectors_erased;
	unsigned char sector_buffer[flash_sector_size];
	struct stat stat;

	if(filename.empty())
		throw(std::string("file name required"));

	if((file_fd = open(filename.c_str(), O_RDONLY, 0)) < 0)
		throw(std::string("file not found"));

	fstat(file_fd, &stat);
	file_offset = stat.st_size;
	length = (file_offset + (flash_sector_size - 1)) / flash_sector_size;

	sectors_skipped = 0;
	sectors_erased = 0;
	sectors_written = 0;

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

		std::cout << " at address: 0x" << std::hex << std::setw(6) << std::setfill('0') << (sector * flash_sector_size) << " (sector " << std::dec << sector << ")";
		std::cout << ", length: " << std::dec << std::setw(0) << (length * flash_sector_size) << " (" << length << " sectors)";
		std::cout << std::endl;

		hash_ctx = EVP_MD_CTX_new();
		EVP_DigestInit_ex(hash_ctx, EVP_sha1(), (ENGINE *)0);

		for(current = sector; current < (sector + length); current++)
		{
			memset(sector_buffer, 0xff, flash_sector_size);

			if((read(file_fd, sector_buffer, flash_sector_size)) <= 0)
				throw(std::string("i/o error in read"));

			if((file_offset = lseek(file_fd, 0, SEEK_CUR)) < 0)
				throw(std::string("i/o error in seek"));

			EVP_DigestUpdate(hash_ctx, sector_buffer, flash_sector_size);

			write_sector(command_channel, current, sector_buffer, sectors_written, sectors_erased, sectors_skipped, simulate, verbose);

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
				std::cout << "sent "		<< std::setw(4) << (file_offset / 1024) << " kbytes";
				std::cout << " in "			<< std::setw(5) << std::setprecision(2) << std::fixed << duration << " seconds";
				std::cout << " at rate "	<< std::setw(4) << std::setprecision(0) << std::fixed << rate << " kbytes/s";
				std::cout << ", sent "		<< std::setw(3) << (current - sector + 1) << " sectors";
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
		if(!verbose)
			std::cout << std::endl;

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

		hash_size = sha1_hash_size;
		EVP_DigestFinal_ex(hash_ctx, hash, &hash_size);
		EVP_MD_CTX_free(hash_ctx);

		sha_local_hash_text = sha_hash_to_text(hash);

		get_checksum(command_channel, sector, length, sha_remote_hash_text, verbose);

		if(verbose)
		{
			std::cout << "local checksum:  " << sha_local_hash_text << std::endl;
			std::cout << "remote checksum: " << sha_remote_hash_text << std::endl;
		}

		if(sha_local_hash_text != sha_remote_hash_text)
			throw(std::string("checksum failed: SHA hash differs, local: ") +  sha_local_hash_text + ", remote: " + sha_remote_hash_text);

		std::cout << "checksum OK" << std::endl;
		std::cout << "write finished" << std::endl;
	}
}

static void command_verify(GenericSocket &command_channel, const std::string &filename, int sector, bool verbose)
{
	int64_t file_offset;
	int file_fd;
	int current, sectors;
	struct timeval time_start, time_now;
	std::string send_string;
	std::string operation;
	std::vector<int> int_value;
	std::vector<std::string> string_value;
	std::string local_data, remote_data;
	struct stat stat;

	if(filename.empty())
		throw(std::string("file name required"));

	if((file_fd = open(filename.c_str(), O_RDONLY)) < 0)
		throw(std::string("can't open file"));

	fstat(file_fd, &stat);
	file_offset = stat.st_size;
	sectors = (file_offset + (flash_sector_size - 1)) / flash_sector_size;

	try
	{
		gettimeofday(&time_start, 0);

		std::cout << "start verify from 0x" << std::hex << sector * flash_sector_size << " (" << std::dec << sector << ")";
		std::cout << ", length: 0x" << std::hex << sectors * flash_sector_size << " (" << std::dec << sectors << ")";
		std::cout << std::endl;

		for(current = sector; current < (sector + sectors); current++)
		{
			local_data.resize(flash_sector_size);
			memset(local_data.data(), 0xff, flash_sector_size);

			if(read(file_fd, local_data.data(), flash_sector_size) <= 0)
				throw(std::string("i/o error in read"));

			if((file_offset = lseek(file_fd, 0, SEEK_CUR)) < 0)
				throw(std::string("i/o error in seek"));

			read_sector(command_channel, current, remote_data, verbose);

			if(local_data != remote_data)
				throw((boost::format("data mismatch, sector %u") % current).str());

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
				std::cout << ", received "	<< std::setw(2) << (current - sector) << " sectors";
				std::cout << ", "			<< std::setw(3) << (((file_offset + flash_sector_size) * 100) / (sectors * flash_sector_size)) << "%       \r";
				std::cout.flush();
			}
		}
	}
	catch(const std::string &e)
	{
		if(!verbose)
			std::cout << std::endl;

		close(file_fd);
		throw;
	}

	close(file_fd);

	if(!verbose)
		std::cout << std::endl;

	std::cout << "verify OK" << std::endl;
}

void command_benchmark(GenericSocket &command_channel, int length, bool verbose)
{
	unsigned int phase, retries, attempt, iterations, current;
	uint8_t sector_buffer[flash_sector_size];
	std::string command;
	std::string expect;
	std::string reply;
	struct timeval time_start, time_now;
	std::vector<int> int_value;
	std::vector<std::string> string_value;
	int seconds, useconds;
	double duration, rate;

	iterations = 1024;
	memset(sector_buffer, 0x00, flash_sector_size);

	for(phase = 0; phase < 2; phase++)
	{
		retries = 0;
		gettimeofday(&time_start, 0);

		if(phase == 0)
		{
			command = (boost::format("flash-bench %u") % length).str();
			expect = (boost::format("OK flash-bench: sending %u bytes\n.*") % length).str();
		}
		else
		{
			command = "flash-bench 0";
			command.append(ota_data_offset - command.length(), ' ');
			command.append((const char *)sector_buffer, length);
			expect = "OK flash-bench: sending 0 bytes\n.*";
		}

		for(current = 0; current < iterations; current++)
		{
			for(attempt = max_attempts; attempt > 0; attempt--)
			{
				try
				{
					process(command_channel, command, reply, expect, string_value, int_value, verbose);

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
						std::cout << ", "			<< std::setw(3) << (((current + 1) * 100) / iterations) << "%       \r";
						std::cout.flush();
					}
				}
				catch(const std::string &e)
				{
					if(verbose)
						std::cout << e << std::endl;

					command_channel.drain();

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

static void command_image_send_sector(GenericSocket &command_channel,
		int current_sector, const unsigned char *buffer, unsigned int length,
		unsigned int current_x, unsigned int current_y, unsigned int depth, bool verbose)
{
	enum { attempts =  8 };
	std::string command;
	std::vector<int> int_value;
	std::vector<std::string> string_value;
	std::string reply;
	unsigned int pixels;

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

		command = (boost::format("display-plot %u %u %u\n") % pixels % current_x % current_y).str();
		command.append(ota_data_offset - command.length(), ' ');
		command.append((const char *)buffer, length);

		process(command_channel, command, reply, "display plot success: yes\n", string_value, int_value, verbose);
	}
	else
	{
		unsigned int sectors_written, sectors_erased, sectors_skipped;
		write_sector(command_channel, current_sector, buffer, sectors_written, sectors_erased, sectors_skipped, false, verbose);
	}
}

static void command_image(GenericSocket &command_channel, int image_slot, const std::string &filename,
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
						if((current_buffer / 8) + 1 > flash_sector_size)
						{
							command_image_send_sector(command_channel, current_sector, sector_buffer, current_buffer / 8,
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

						if((current_buffer + 2) > flash_sector_size)
						{
							command_image_send_sector(command_channel, current_sector, sector_buffer, current_buffer,
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

			command_image_send_sector(command_channel, current_sector, sector_buffer, current_buffer,
					start_x, start_y, depth, verbose);
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

static void command_send(GenericSocket &send_socket, bool verbose, bool dont_wait, std::string args)
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
			if(!send_socket.send_unicast(arg))
			{
				if(verbose)
					std::cout << "send failed, attempt #" << attempt << std::endl;

				goto retry;
			}

			if(!send_socket.receive_unicast(reply))
			{
				if(verbose)
					std::cout << "receive failed, attempt #" << attempt << std::endl;

				goto retry;
			}

			attempt = 0;
			goto exit;

retry:
			send_socket.drain();
		}
exit:

		if(attempt != 0)
			throw(std::string("send/receive failed"));

		std::cout << reply;
	}
}

void command_multicast(GenericSocket &multicast_socket, bool dontwait, const std::string &args)
{
	std::string reply;

	if(!multicast_socket.send_multicast(args))
		throw(std::string("mulicast send failed"));

	if(!dontwait)
	{
		if(!multicast_socket.receive_multicast(reply))
			throw(std::string("multicast receive failed"));

		std::cout << reply << std::endl;
	}
}

void commit_ota(GenericSocket &command_channel, bool verbose, unsigned int flash_slot, unsigned int sector, bool reset, bool notemp)
{
	std::string send_string;
	std::string reply;
	std::vector<std::string> string_value;
	std::vector<int> int_value;

	send_string = (boost::format("flash-select %u %u") % flash_slot % (notemp ? 1 : 0)).str();
	process(command_channel, send_string, reply, flash_select_expect, string_value, int_value, verbose);

	if(int_value[0] != (int)flash_slot)
		throw(std::string("flash-select failed, local slot (") + std::to_string(flash_slot) + ") != remote slot (" + std::to_string(int_value[0]) + ")");

	if(int_value[1] != (int)sector)
		throw(std::string("flash-select failed, local sector != remote sector"));

	if(int_value[2] != notemp ? 1 : 0)
		throw(std::string("flash-select failed, local permanent != remote permanent"));

	std::cout << "selected ";

	if(!notemp)
		std::cout << "one time";

	std::cout << " boot slot" << std::endl;

	if(!reset)
		return;

	std::cout << "rebooting" << std::endl;
	command_channel.send_unicast(std::string("reset"));
	usleep(200000);
	command_channel.drain();
	command_channel.disconnect();
	usleep(1000000);
	command_channel.connect();
	std::cout << "reboot finished" << std::endl;

	process(command_channel, "flash-info", reply, flash_info_expect, string_value, int_value, verbose);

	if(int_value[0] != (int)flash_slot)
		throw(std::string("boot failed, requested slot: ") + std::to_string(flash_slot) + ", active slot: " + std::to_string(int_value[0]));

	if(!notemp)
	{
		std::cout << "boot succeeded, permanently selecting boot slot: " << flash_slot << std::endl;

		send_string = (boost::format("flash-select %u 1") % flash_slot).str();
		process(command_channel, send_string, reply, flash_select_expect, string_value, int_value, verbose);

		if(int_value[0] != (int)flash_slot)
			throw(std::string("flash-select failed, local slot (") + std::to_string(flash_slot) + ") != remote slot (" + std::to_string(int_value[0]) + ")");

		if(int_value[1] != (int)sector)
			throw(std::string("flash-select failed, local sector != remote sector"));

		if(int_value[2] != 1)
			throw(std::string("flash-select failed, local permanent != remote permanent"));
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
		std::string filename;
		std::string start_string;
		std::string length_string;
		int start;
		int image_slot;
		int image_timeout;
		int dim_x, dim_y, depth;
		int broadcast_group;
		unsigned int length;
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
		bool cmd_benchmark = false;
		bool cmd_image = false;
		bool cmd_image_epaper = false;
		bool cmd_broadcast = false;
		bool cmd_multicast = false;
		bool cmd_read = false;
		bool cmd_info = false;
		bool no_provide_checksum = false;
		bool no_request_checksum = false;
		bool raw = false;
		unsigned int selected;

		options.add_options()
			("info,i",					po::bool_switch(&cmd_info)->implicit_value(true),					"INFO")
			("read,R",					po::bool_switch(&cmd_read)->implicit_value(true),					"READ")
			("verify,V",				po::bool_switch(&cmd_verify)->implicit_value(true),					"VERIFY")
			("simulate,S",				po::bool_switch(&cmd_simulate)->implicit_value(true),				"WRITE simulate")
			("write,W",					po::bool_switch(&cmd_write)->implicit_value(true),					"WRITE")
			("benchmark,B",				po::bool_switch(&cmd_benchmark)->implicit_value(true),				"BENCHMARK")
			("image,I",					po::bool_switch(&cmd_image)->implicit_value(true),					"SEND IMAGE")
			("epaper-image,e",			po::bool_switch(&cmd_image_epaper)->implicit_value(true),			"SEND EPAPER IMAGE (uc8151d connected to host)")
			("broadcast,b",				po::bool_switch(&cmd_broadcast)->implicit_value(true),				"BROADCAST SENDER send broadcast message")
			("multicast,M",				po::bool_switch(&cmd_multicast)->implicit_value(true),				"MULTICAST SENDER send multicast message")
			("host,h",					po::value<std::vector<std::string> >(&host_args)->required(),		"host or broadcast address or multicast group to use")
			("verbose,v",				po::bool_switch(&verbose)->implicit_value(true),					"verbose output")
			("udp,u",					po::bool_switch(&use_udp)->implicit_value(true),					"use UDP instead of TCP")
			("filename,f",				po::value<std::string>(&filename),									"file name")
			("start,s",					po::value<std::string>(&start_string)->default_value("-1"),			"send/receive start address (OTA is default)")
			("length,l",				po::value<std::string>(&length_string)->default_value("0x1000"),	"read length")
			("command-port,p",			po::value<std::string>(&command_port)->default_value("24"),			"command port to connect to")
			("nocommit,n",				po::bool_switch(&nocommit)->implicit_value(true),					"don't commit after writing")
			("noreset,N",				po::bool_switch(&noreset)->implicit_value(true),					"don't reset after commit")
			("notemp,t",				po::bool_switch(&notemp)->implicit_value(true),						"don't commit temporarily, commit to flash")
			("dontwait,d",				po::bool_switch(&dont_wait)->implicit_value(true),					"don't wait for reply on message")
			("image_slot,x",			po::value<int>(&image_slot)->default_value(-1),						"send image to flash slot x instead of frame buffer")
			("image_timeout,y",			po::value<int>(&image_timeout)->default_value(5000),				"freeze frame buffer for y ms after sending")
			("no-provide-checksum,1",	po::bool_switch(&no_provide_checksum)->implicit_value(true),		"do not provide checksum")
			("no-request-checksum,2",	po::bool_switch(&no_request_checksum)->implicit_value(true),		"do not request checksum")
			("raw,r",					po::bool_switch(&raw)->implicit_value(true),						"do not use packet encapsulation")
			("broadcast-group,g",		po::value<int>(&broadcast_group)->default_value(-1),				"select broadcast group");

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

		if(cmd_read)
			selected++;

		if(cmd_write)
			selected++;

		if(cmd_simulate)
			selected++;

		if(cmd_verify)
			selected++;

		if(cmd_benchmark)
			selected++;

		if(cmd_image)
			selected++;

		if(cmd_image_epaper)
			selected++;

		if(cmd_info)
			selected++;

		if(cmd_broadcast)
			selected++;

		if(cmd_multicast)
			selected++;

		if(selected > 1)
			throw(std::string("specify one of write/simulate/verify/image/epaper-image/read/info"));

		GenericSocket command_channel(host, command_port, use_udp, verbose, !!cmd_broadcast, !!cmd_multicast, no_provide_checksum, no_request_checksum, raw, broadcast_group);

		if(selected == 0)
			command_send(command_channel, verbose, dont_wait, args);
		else
		{
			if(cmd_broadcast || cmd_multicast)
				command_multicast(command_channel, dont_wait, args);
			else
			{
				start = -1;

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
				unsigned int flash_slot, flash_address[2];

				try
				{
					process(command_channel, "flash-info", reply, flash_info_expect, string_value, int_value, verbose);
				}
				catch(std::string &e)
				{
					throw(std::string("flash incompatible image: ") + e);
				}

				flash_slot = int_value[0];
				flash_address[0] = int_value[1];
				flash_address[1] = int_value[2];
				dim_x = int_value[3];
				dim_y = int_value[4];
				depth = int_value[5];

				std::cout << "flash update available, current slot: " << flash_slot;
				std::cout << ", address[0]: 0x" << std::hex << (flash_address[0] * flash_sector_size) << " (sector " << std::dec << flash_address[0] << ")";
				std::cout << ", address[1]: 0x" << std::hex << (flash_address[1] * flash_sector_size) << " (sector " << std::dec << flash_address[1] << ")";
				std::cout << ", display graphical dimensions: " << dim_x << "x" << dim_y << " px at depth " << depth;
				std::cout << std::endl;

				if(start == -1)
				{
					if(cmd_write || cmd_simulate || cmd_verify || cmd_info)
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
					command_read(command_channel, filename, start, length, verbose);
				else
					if(cmd_verify)
						command_verify(command_channel, filename, start, verbose);
					else
						if(cmd_simulate)
							command_write(command_channel, filename, start, verbose, true, false);
						else
							if(cmd_write)
							{
								command_write(command_channel, filename, start, verbose, false, otawrite);

								if(otawrite && !nocommit)
									commit_ota(command_channel, verbose, flash_slot, start, !noreset, notemp);
							}
							else
								if(cmd_benchmark)
									command_benchmark(command_channel, length, verbose);
								else
									if(cmd_image)
										command_image(command_channel, image_slot, filename, dim_x, dim_y, depth, image_timeout, verbose);
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
