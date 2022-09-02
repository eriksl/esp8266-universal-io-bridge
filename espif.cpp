#include "espif.h"

extern "C" {
#define __espif__
#define assert_size(type, size) static_assert(sizeof(type) == size, "sizeof(" #type ") != " #size)
#define assert_field(name, field, offset) static_assert(offsetof(name, field) == offset)
#define attr_packed __attribute__ ((__packed__))
#define _Static_assert static_assert
#include "ota.h"
#undef assert_size
#undef attr_packed
}

namespace po = boost::program_options;

enum
{
	flash_sector_size = 4096,
	md5_hash_size = 16,
	sha1_hash_size = 20,
};

static bool option_raw = false;
static bool option_verbose = false;
static bool option_no_provide_checksum = false;
static bool option_no_request_checksum = false;
static bool option_use_tcp = false;
static unsigned int option_broadcast_group_mask = 0;

static const std::string flash_info_expect("OK flash function available, slots: 2, current: ([0-9]+), sectors: \\[ ([0-9]+), ([0-9]+) \\], display: ([0-9]+)x([0-9]+)px@([0-9]+)");
static const std::string flash_select_expect("OK flash-select: slot ([0-9]+) selected, sector ([0-9]+), permanent ([0-1])");

static uint32_t MD5_trunc_32(const std::string &data)
{
	uint8_t hash[md5_hash_size];
	uint32_t checksum;
	unsigned int hash_size;
	EVP_MD_CTX *hash_ctx;

	hash_ctx = EVP_MD_CTX_new();
	EVP_DigestInit_ex(hash_ctx, EVP_md5(), (ENGINE *)0);
	EVP_DigestUpdate(hash_ctx, data.data(), data.length());
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
		struct sockaddr_in saddr;
		bool tcp, broadcast, multicast;

	public:
		GenericSocket(const std::string &host, const std::string &port, bool tcp, bool broadcast, bool multicast);
		~GenericSocket();

		bool send(std::string &data, int timeout = -1);
		bool receive(std::string &data, int timeout = -1, struct sockaddr_in *remote_host = nullptr);
		void drain();
		void connect();
		void disconnect();
};

GenericSocket::GenericSocket(const std::string &host_in, const std::string &service_in, bool tcp_in,
		bool broadcast_in, bool multicast_in)
	: socket_fd(-1), service(service_in), tcp(tcp_in), broadcast(broadcast_in), multicast(multicast_in)
{
	memset(&saddr, 0, sizeof(saddr));

	if(multicast)
		host = std::string("239.255.255.") + host_in;
	else
		host = host_in;

	if(multicast || broadcast)
		tcp = false;

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

	if((socket_fd = socket(AF_INET, tcp ? SOCK_STREAM : SOCK_DGRAM, 0)) < 0)
		throw(std::string("socket failed"));

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = tcp ? SOCK_STREAM : SOCK_DGRAM;
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
		int arg = 1;

		if(setsockopt(socket_fd, SOL_SOCKET, SO_BROADCAST, &arg, sizeof(arg)))
		{
			if(option_verbose)
				perror("setsockopt SO_BROADCAST\n");
			throw(std::string("set broadcast"));
		}
	}

	if(multicast)
	{
		struct ip_mreq mreq;
		int arg = 3;

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

	if(tcp && ::connect(socket_fd, (const struct sockaddr *)&saddr, sizeof(saddr)))
		throw(std::string("connect failed"));
}

void GenericSocket::disconnect()
{
	if(socket_fd >= 0)
		close(socket_fd);

	socket_fd = -1;
}

bool GenericSocket::send(std::string &data, int timeout)
{
	struct pollfd pfd;
	int length;

	if(timeout < 0)
		timeout = 0;

	pfd.fd = socket_fd;
	pfd.events = POLLOUT | POLLERR | POLLHUP;
	pfd.revents = 0;

	if(data.length() == 0)
	{
		if(option_verbose)
			std::cout << "send: empty buffer" << std::endl;

		return(true);
	}

	if(poll(&pfd, 1, timeout) != 1)
	{
		if(option_verbose)
			std::cout << "send: timeout" << std::endl;

		return(false);
	}

	if(pfd.revents & (POLLERR | POLLHUP))
		return(false);

	if(tcp)
	{
		if((length = ::send(socket_fd, data.data(), data.length(), 0)) <= 0)
			return(false);
	}
	else
	{
		if((length = ::sendto(socket_fd, data.data(), data.length(), 0, (const struct sockaddr *)&this->saddr, sizeof(this->saddr))) <= 0)
			return(false);
	}

	data.erase(0, length);

	return(true);
}

bool GenericSocket::receive(std::string &data, int timeout, struct sockaddr_in *remote_host)
{
	int length;
	char buffer[2 * flash_sector_size];
	socklen_t remote_host_length = sizeof(*remote_host);
	struct pollfd pfd = { .fd = socket_fd, .events = POLLIN | POLLERR | POLLHUP, .revents = 0 };

	if(timeout < 0)
		timeout = tcp ? 2000 : 500;

	if(poll(&pfd, 1, timeout) != 1)
	{
		if(option_verbose)
			std::cout << std::string("receive: timeout, length: ") << std::to_string(data.length()) << std::endl;
		return(false);
	}

	if(pfd.revents & POLLERR)
	{
		if(option_verbose)
			std::cout << "receive: POLLERR" << std::endl;
		return(false);
	}

	if(pfd.revents & POLLHUP)
	{
		if(option_verbose)
			std::cout << "receive: POLLHUP" << std::endl;
		return(false);
	}

	if(tcp)
	{
		if((length = ::recv(socket_fd, buffer, sizeof(buffer) - 1, 0)) <= 0)
		{
			if(option_verbose)
				std::cout << "tcp receive: length <= 0" << std::endl;
			return(false);
		}
	}
	else
	{
		if((length = ::recvfrom(socket_fd, buffer, sizeof(buffer) - 1, 0, (sockaddr *)remote_host, &remote_host_length)) <= 0)
		{
			if(option_verbose)
				std::cout << "udp receive: length <= 0" << std::endl;
			return(false);
		}
	}

	data.append(buffer, (size_t)length);

	return(true);
}

void GenericSocket::drain()
{
	struct pollfd pfd;
	enum { drain_packets = 4 };
	char *buffer = (char *)alloca(flash_sector_size * drain_packets);
	int length;

	if(option_verbose)
		std::cout << "draining..." << std::endl;

	pfd.fd = socket_fd;
	pfd.events = POLLIN | POLLERR | POLLHUP;
	pfd.revents = 0;

	if(poll(&pfd, 1, tcp ? 10000 : 500) != 1)
		return;

	if(pfd.revents & (POLLERR | POLLHUP))
		return;

	if(tcp)
	{
		if((length = ::recv(socket_fd, buffer, flash_sector_size * drain_packets, 0)) < 0)
			return;
	}
	else
	{
		if((length = ::recvfrom(socket_fd, buffer, flash_sector_size * drain_packets, 0, (struct sockaddr *)0, 0)) < 0)
			return;
	}

	if(option_verbose)
		std::cout << "drained " << length << " bytes" << std::endl;
}

class Packet
{
	public:
		Packet(Packet &) = delete;
		Packet();
		Packet(const std::string *data, const std::string *oob_data = nullptr);
		void clear();
		void append_data(const std::string &);
		void append_oob_data(const std::string &);
		std::string encapsulate(uint32_t &transaction_id, bool raw, bool provide_checksum, bool request_checksum, unsigned int broadcast_group_mask);
		bool decapsulate(uint32_t transaction_id, std::string *data = nullptr, std::string *oob_data = nullptr, bool *raw = nullptr);
		bool complete();

	private:
		std::string data;
		std::string oob_data;
		packet_header_t packet_header;

		void clear_packet_header();
};

void Packet::clear_packet_header()
{
	packet_header.soh = 0;
	packet_header.version = 0;
	packet_header.id = 0;
	packet_header.length = 0;
	packet_header.data_offset = 0;
	packet_header.data_pad_offset = 0;
	packet_header.oob_data_offset = 0;
	packet_header.broadcast_groups = 0;
	packet_header.flags = 0;
	packet_header.transaction_id = 0;
	packet_header.spare_0 = 0;
	packet_header.spare_1 = 0;
	packet_header.checksum = 0;
}

Packet::Packet()
{
	clear();
}

Packet::Packet(const std::string *data_in, const std::string *oob_data_in)
{
	clear();

	data = *data_in;

	if(oob_data_in)
		oob_data = *oob_data_in;
}

void Packet::clear()
{
	data.clear();
	oob_data.clear();
	clear_packet_header();
}

void Packet::append_data(const std::string &data_in)
{
	data.append(data_in);
}

void Packet::append_oob_data(const std::string &oob_data_in)
{
	oob_data.append(oob_data_in);
}

std::string Packet::encapsulate(uint32_t &transaction_id, bool raw, bool provide_checksum, bool request_checksum, unsigned int broadcast_group_mask)
{
	std::string pad;
	std::string packet;
	struct timeval tv;
	gettimeofday(&tv, nullptr);
	boost::random::mt19937 prg(tv.tv_usec);

	if(raw)
	{
		packet = data;

		if((packet.length() > 0) && (packet.back() != '\n'))
			packet.append(1, '\n');

		if(oob_data.length() > 0)
		{
			packet.append(1, '\0');

			while((packet.length() % 4) != 0)
				packet.append(1, '\0');

			packet.append(oob_data);
		}
	}
	else
	{
		clear_packet_header();

		if(oob_data.length() > 0)
			while(((data.length() + pad.length()) % 4) != 0)
				pad.append(1, '\0');

		packet_header.soh = packet_header_soh;
		packet_header.version = packet_header_version;
		packet_header.id = packet_header_id;
		packet_header.length = sizeof(packet_header) + data.length() + pad.length() + oob_data.length();
		packet_header.data_offset = sizeof(packet_header);
		packet_header.data_pad_offset = sizeof(packet_header) + data.length();
		packet_header.oob_data_offset = sizeof(packet_header) + data.length() + pad.length();
		packet_header.transaction_id = transaction_id = prg();

		if(request_checksum)
			packet_header.flag.md5_32_requested = 1;

		packet_header.broadcast_groups = broadcast_group_mask & ((1 << (sizeof(packet_header.broadcast_groups) * 8)) - 1);

		if(provide_checksum)
		{
			packet_header.flag.md5_32_provided = 1;
			std::string packet_checksum = std::string((const char *)&packet_header, sizeof(packet_header)) + data + pad + oob_data;
			packet_header.checksum = MD5_trunc_32(packet_checksum);
		}

		packet = std::string((const char *)&packet_header, sizeof(packet_header)) + data + pad + oob_data;
	}

	return(packet);
}

bool Packet::decapsulate(uint32_t transaction_id, std::string *data_in, std::string *oob_data_in, bool *rawptr)
{
	bool raw = false;
	unsigned int our_checksum;

	if(data.length() < sizeof(packet_header))
		raw = true;
	else
	{
		packet_header = *(packet_header_t *)data.data();

		if((packet_header.soh != packet_header_soh) || (packet_header.id != packet_header_id))
			raw = true;
	}

	if(raw)
	{
		size_t padding_offset, oob_data_offset;

		clear_packet_header();

		padding_offset = data.find('\0', 0);

		if(padding_offset == std::string::npos)
			oob_data.clear();
		else
		{
			oob_data_offset = padding_offset + 1;

			while((oob_data_offset % 4) != 0)
				oob_data_offset++;

			if(oob_data_offset < data.length())
				oob_data = data.substr(oob_data_offset);
			else
			{
				if(option_verbose)
					std::cout << "invalid raw oob data padding" << std::endl;

				oob_data.clear();
			}

			data.erase(padding_offset);
		}
	}
	else
	{
		packet_header = *(packet_header_t *)data.data();

		if(packet_header.version != packet_header_version)
		{
			if(option_verbose)
				std::cout << "decapsulate: wrong version packet received: " << packet_header.version << std::endl;

			return(false);
		}

		if(packet_header.flag.md5_32_provided)
		{
			packet_header_t packet_header_checksum = packet_header;
			std::string data_checksum;

			packet_header_checksum.checksum = 0;
			data_checksum = std::string((const char *)&packet_header_checksum, sizeof(packet_header_checksum)) + data.substr(packet_header.data_offset);
			our_checksum = MD5_trunc_32(data_checksum);

			if(our_checksum != packet_header.checksum)
			{
				if(option_verbose)
					std::cout << "decapsulate: invalid checksum, ours: " << std::hex << our_checksum << ", theirs: " << packet_header.checksum << std::dec << std::endl;

				return(false);
			}
		}

		if(packet_header.transaction_id != transaction_id)
		{
			if(option_verbose)
				std::cout << "duplicate packet" << std::endl;

			return(false);
		}

		if((packet_header.oob_data_offset != packet_header.length) && ((packet_header.oob_data_offset % 4) != 0))
		{
			if(option_verbose)
				std::cout << "packet oob data padding invalid: " << packet_header.oob_data_offset << std::endl;

			oob_data.clear();
		}
		else
		{
			oob_data = data.substr(packet_header.oob_data_offset);
			data = data.substr(packet_header.data_offset, packet_header.data_pad_offset - packet_header.data_offset);
		}
	}

	if((data.back() == '\n') || (data.back() == '\r'))
		data.pop_back();

	if((data.back() == '\n') || (data.back() == '\r'))
		data.pop_back();

	if(data_in)
		*data_in = data;

	if(oob_data_in)
		*oob_data_in = oob_data;

	if(rawptr)
		*rawptr = raw;

	return(true);
}

bool Packet::complete()
{
	if(data.length() == 0)
		return(false);

	if(data.length() < sizeof(packet_header))
		return(data.back() == '\n');

	packet_header = *(packet_header_t *)data.data();

	if((packet_header.soh != packet_header_soh) ||
			(packet_header.id != packet_header_id))
		return((data.find('\0') != std::string::npos) || (data.back() == '\n'));

	return(data.length() >= packet_header.length);
}

static std::string sha_hash_to_text(const unsigned char *hash)
{
	unsigned int current;
	std::stringstream hash_string;

	for(current = 0; current < sha1_hash_size; current++)
		hash_string << std::hex << std::setw(2) << std::setfill('0') << (unsigned int)hash[current];

	return(hash_string.str());
}

static int process(GenericSocket &channel, const std::string &data, const std::string *oob_data, std::string &reply_data, std::string *reply_oob_data, const std::string &match,
		std::vector<std::string> &string_value, std::vector<int> &int_value)
{
	boost::regex re(match);
	boost::smatch capture;
	unsigned int captures;
	unsigned int attempt;
	Packet send_packet(&data, oob_data);
	std::string send_data;
	std::string packet;
	Packet receive_packet;
	std::string receive_data;
	uint32_t transaction_id;

	if(option_verbose)
	{
		int length = 0;

		std::cout << "> send (" << data.length() << "): \"";

		for(const auto &it : data)
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

	packet = send_packet.encapsulate(transaction_id, option_raw, !option_no_provide_checksum, !option_no_request_checksum, option_broadcast_group_mask);

	for(attempt = 0; attempt < 4; attempt++)
	{
		send_data = packet;

		while(send_data.length() > 0)
		{
			if(!channel.send(send_data))
			{
				if(option_verbose)
					std::cout << "process: send failed #" << attempt << std::endl;

				goto retry;
			}
		}

		receive_packet.clear();

		while(!receive_packet.complete())
		{
			receive_data.clear();

			if(!channel.receive(receive_data))
			{
				if(option_verbose)
					std::cout << "process: receive failed #" << attempt << std::endl;

				goto retry;
			}

			receive_packet.append_data(receive_data);
		}

		if(!receive_packet.decapsulate(transaction_id, &reply_data, reply_oob_data))
			goto retry;

		break;
retry:
		if(option_verbose)
			std::cout << "retry " << attempt << "..." << std::endl;

		channel.drain();
	}

	if(option_verbose && (attempt > 0))
		std::cout << "success at attempt " << attempt << std::endl;

	if(attempt >= 4)
		throw(std::string("process: receive failed"));

	if(option_verbose)
		std::cout << "< received (" << reply_data.length() << "): \"" << reply_data << "\"" << std::endl;

	if(!boost::regex_match(reply_data, capture, re))
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

	return(attempt);
}

static int read_sector(GenericSocket &command_channel, unsigned int sector, std::string &data)
{
	std::string reply;
	std::vector<int> int_value;
	std::vector<std::string> string_value;
	int retries;

	try
	{
		retries = process(command_channel, std::string("flash-read ") + std::to_string(sector) + "\n", nullptr, reply, &data,
				"OK flash-read: read sector ([0-9]+)", string_value, int_value);
	}
	catch(std::string &error)
	{
		error = std::string("read_sector: ") + error;
		throw(error);
	}

	if(data.length() < flash_sector_size)
	{
		if(option_verbose)
		{
			std::cout << "flash sector read failed: incorrect length";
			std::cout << ", expected: " << flash_sector_size << ", received: " << data.length();
			std::cout << ", reply: " << reply << std::endl;
		}

		throw("read_sector failed");
	}

	if(int_value[0] != (int)sector)
	{
		if(option_verbose)
			std::cout << "flash sector read failed: local sector #" << sector << " != remote sector #" << int_value[0] << std::endl;

		throw("read_sector_failed");
	}

	return(retries);
}

static int write_sector(GenericSocket &command_channel, unsigned int sector, const std::string &data,
		unsigned int &written, unsigned int &erased, unsigned int &skipped, bool simulate)
{
	std::string command;
	std::string reply;
	std::vector<int> int_value;
	std::vector<std::string> string_value;
	int retries;

	command = (boost::format("flash-write %u %u") % (simulate ? 0 : 1) % sector).str();

	try
	{
		retries = process(command_channel, command, &data, reply, nullptr, "OK flash-write: written mode ([01]), sector ([0-9]+), same ([01]), erased ([01])", string_value, int_value);
	}
	catch(std::string &error)
	{
		if(option_verbose)
			std::cout << "flash sector write failed: " << error << ", reply: " << reply << std::endl;

		error = std::string("write_sector: ") + error;
		throw(error);
	}

	if(int_value[0] != (simulate ? 0 : 1))
	{
		if(option_verbose)
			std::cout << boost::format("flash sector write failed: mode local: %u != mode remote %u\n") % (simulate ? 0 : 1) % int_value[0];

		throw("write_sector failed");
	}

	if(int_value[1] != (int)sector)
	{
		if(option_verbose)
			std::cout << boost::format("flash sector write failed: sector local: %u != sector remote %u\n") % sector % int_value[0];

		throw("write_sector failed");
	}

	if(int_value[2] != 0)
		skipped++;
	else
		written++;

	if(int_value[3] != 0)
		erased++;

	return(retries);
}

static void get_checksum(GenericSocket &command_channel, unsigned int sector, unsigned int sectors, std::string &checksum)
{
	std::string reply;
	std::vector<int> int_value;
	std::vector<std::string> string_value;

	try
	{
		process(command_channel, std::string("flash-checksum ") + std::to_string(sector) + " " + std::to_string(sectors) + "\n", nullptr,
				reply, nullptr, "OK flash-checksum: checksummed ([0-9]+) sectors from sector ([0-9]+), checksum: ([0-9a-f]+)", string_value, int_value);
	}
	catch(std::string &error)
	{
		if(option_verbose)
			std::cout << "flash sector checksum failed: " << error << ", reply: " << reply << std::endl;

		error = std::string("get_checksum ") + error;
		throw(error);
	}

	if(int_value[0] != (int)sectors)
	{
		if(option_verbose)
			std::cout << "flash sector checksum failed: local sectors (" << sectors + ") != remote sectors (" << int_value[0] << ")" << std::endl;

		throw("get_checksum failed");
	}

	if(int_value[1] != (int)sector)
	{
		if(option_verbose)
			std::cout << "flash sector checksum failed: local start sector (" << sector << ") != remote start sector (" << int_value[1] << ")" << std::endl;

		throw("get_checksum failed");
	}

	checksum = string_value[2];
}

static void command_read(GenericSocket &command_channel, const std::string &filename, int sector, int sectors)
{
	int file_fd, offset, current, retries;
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

		if(option_verbose)
		{
			std::cout << "start read from 0x" << std::hex << sector * flash_sector_size << " (" << std::dec << sector << ")";
			std::cout << ", length: 0x" << std::hex << sectors * flash_sector_size << " (" << std::dec << sectors << ")";
			std::cout << std::endl;
		}

		hash_ctx = EVP_MD_CTX_new();
		EVP_DigestInit_ex(hash_ctx, EVP_sha1(), (ENGINE *)0);

		retries = 0;

		for(current = sector, offset = 0; current < (sector + sectors); current++)
		{
			retries += read_sector(command_channel, current, data);

			if(write(file_fd, data.data(), data.length()) <= 0)
				throw(std::string("i/o error in write"));

			EVP_DigestUpdate(hash_ctx, (const unsigned char *)data.data(), data.length());

			offset += data.length();

			if(!option_verbose)
			{
				int seconds, useconds;
				double duration, rate;

				gettimeofday(&time_now, 0);

				seconds = time_now.tv_sec - time_start.tv_sec;
				useconds = time_now.tv_usec - time_start.tv_usec;
				duration = seconds + (useconds / 1000000.0);
				rate = offset / 1024.0 / duration;

				std::cout << std::setfill(' ');
				std::cout << "received "	<< std::setw(3) << (offset / 1024) << " kbytes";
				std::cout << " in "			<< std::setw(5) << std::setprecision(2) << std::fixed << duration << " seconds";
				std::cout << " at rate "	<< std::setw(3) << std::setprecision(0) << std::fixed << rate << " kbytes/s";
				std::cout << ", received "	<< std::setw(3) << (current - sector) << " sectors";
				std::cout << ", retries "   << std::setw(2) << retries;
				std::cout << ", "			<< std::setw(3) << ((offset * 100) / (sectors * flash_sector_size)) << "%       \r";
				std::cout.flush();
			}
		}
	}
	catch(const std::string &e)
	{
		if(!option_verbose)
			std::cout << std::endl;

		close(file_fd);
		throw;
	}

	close(file_fd);

	if(!option_verbose)
		std::cout << std::endl;

	std::cout << "checksumming " << sectors << " sectors from " << sector << "..." << std::endl;

	hash_size = sha1_hash_size;
	EVP_DigestFinal_ex(hash_ctx, hash, &hash_size);
	EVP_MD_CTX_free(hash_ctx);

	sha_local_hash_text = sha_hash_to_text(hash);
	get_checksum(command_channel, sector, sectors, sha_remote_hash_text);

	if(sha_local_hash_text != sha_remote_hash_text)
	{
		if(option_verbose)
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

void command_write(GenericSocket &command_channel, const std::string filename, int sector, bool simulate, bool otawrite)
{
	int file_fd, length, current, offset, retries;
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
	length = (stat.st_size + (flash_sector_size - 1)) / flash_sector_size;

	sectors_skipped = 0;
	sectors_erased = 0;
	sectors_written = 0;
	offset = 0;

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

		retries = 0;

		for(current = sector; current < (sector + length); current++)
		{
			memset(sector_buffer, 0xff, flash_sector_size);

			if((read(file_fd, sector_buffer, flash_sector_size)) <= 0)
				throw(std::string("i/o error in read"));

			EVP_DigestUpdate(hash_ctx, sector_buffer, flash_sector_size);

			retries += write_sector(command_channel, current, std::string((const char *)sector_buffer, sizeof(sector_buffer)), sectors_written, sectors_erased, sectors_skipped, simulate);

			offset += flash_sector_size;

			if(!option_verbose)
			{
				int seconds, useconds;
				double duration, rate;

				gettimeofday(&time_now, 0);

				seconds = time_now.tv_sec - time_start.tv_sec;
				useconds = time_now.tv_usec - time_start.tv_usec;
				duration = seconds + (useconds / 1000000.0);
				rate = offset / 1024.0 / duration;

				std::cout << std::setfill(' ');
				std::cout << "sent "		<< std::setw(4) << (offset / 1024) << " kbytes";
				std::cout << " in "			<< std::setw(5) << std::setprecision(2) << std::fixed << duration << " seconds";
				std::cout << " at rate "	<< std::setw(4) << std::setprecision(0) << std::fixed << rate << " kbytes/s";
				std::cout << ", sent "		<< std::setw(3) << (current - sector + 1) << " sectors";
				std::cout << ", written "	<< std::setw(3) << sectors_written << " sectors";
				std::cout << ", erased "	<< std::setw(3) << sectors_erased << " sectors";
				std::cout << ", skipped "	<< std::setw(3) << sectors_skipped << " sectors";
				std::cout << ", retries "   << std::setw(2) << retries;
				std::cout << ", "			<< std::setw(3) << (((offset + flash_sector_size) * 100) / (length * flash_sector_size)) << "%       \r";
				std::cout.flush();
			}
		}
	}
	catch(std::string &e)
	{
		if(!option_verbose)
			std::cout << std::endl;

		close(file_fd);
		throw;
	}

	close(file_fd);

	if(!option_verbose)
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

		get_checksum(command_channel, sector, length, sha_remote_hash_text);

		if(option_verbose)
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

static void command_verify(GenericSocket &command_channel, const std::string &filename, int sector)
{
	int file_fd, offset;
	int current, sectors;
	struct timeval time_start, time_now;
	std::string send_string;
	std::string operation;
	std::vector<int> int_value;
	std::vector<std::string> string_value;
	std::string local_data, remote_data;
	struct stat stat;
	uint8_t sector_buffer[flash_sector_size];
	int retries;

	if(filename.empty())
		throw(std::string("file name required"));

	if((file_fd = open(filename.c_str(), O_RDONLY)) < 0)
		throw(std::string("can't open file"));

	fstat(file_fd, &stat);
	sectors = (stat.st_size + (flash_sector_size - 1)) / flash_sector_size;
	offset = 0;

	try
	{
		gettimeofday(&time_start, 0);

		if(option_verbose)
		{
			std::cout << "start verify from 0x" << std::hex << sector * flash_sector_size << " (" << std::dec << sector << ")";
			std::cout << ", length: 0x" << std::hex << sectors * flash_sector_size << " (" << std::dec << sectors << ")";
			std::cout << std::endl;
		}

		retries = 0;

		for(current = sector; current < (sector + sectors); current++)
		{
			memset(sector_buffer, 0xff, flash_sector_size);

			if(read(file_fd, sector_buffer, sizeof(sector_buffer)) <= 0)
				throw(std::string("i/o error in read"));

			local_data.assign((const char *)sector_buffer, sizeof(sector_buffer));

			retries += read_sector(command_channel, current, remote_data);

			if(local_data != remote_data)
				throw((boost::format("data mismatch, sector %u") % current).str());

			offset += sizeof(sector_buffer);

			if(!option_verbose)
			{
				int seconds, useconds;
				double duration, rate;

				gettimeofday(&time_now, 0);

				seconds = time_now.tv_sec - time_start.tv_sec;
				useconds = time_now.tv_usec - time_start.tv_usec;
				duration = seconds + (useconds / 1000000.0);
				rate = offset / 1024.0 / duration;

				std::cout << std::setfill(' ');
				std::cout << "received "	<< std::setw(3) << (offset / 1024) << " kbytes";
				std::cout << " in "			<< std::setw(5) << std::setprecision(2) << std::fixed << duration << " seconds";
				std::cout << " at rate "	<< std::setw(3) << std::setprecision(0) << std::fixed << rate << " kbytes/s";
				std::cout << ", received "	<< std::setw(3) << (current - sector) << " sectors";
				std::cout << ", retries "   << std::setw(2) << retries;
				std::cout << ", "			<< std::setw(3) << ((offset * 100) / (sectors * flash_sector_size)) << "%       \r";
				std::cout.flush();
			}
		}
	}
	catch(const std::string &e)
	{
		if(!option_verbose)
			std::cout << std::endl;

		close(file_fd);
		throw;
	}

	close(file_fd);

	if(!option_verbose)
		std::cout << std::endl;

	std::cout << "verify OK" << std::endl;
}

void command_benchmark(GenericSocket &command_channel, int length)
{
	unsigned int phase, retries, iterations, current;
	std::string command;
	std::string data(flash_sector_size, '\0');
	std::string expect;
	std::string reply;
	struct timeval time_start, time_now;
	std::vector<int> int_value;
	std::vector<std::string> string_value;
	int seconds, useconds;
	double duration, rate;

	iterations = 1024;

	for(phase = 0; phase < 2; phase++)
	{
		retries = 0;

		gettimeofday(&time_start, 0);

		for(current = 0; current < iterations; current++)
		{
			if(phase == 0)
				retries += process(command_channel,
						"flash-bench 0",
						&data,
						reply,
						nullptr,
						"OK flash-bench: sending 0 bytes",
						string_value, int_value);
			else
				retries += process(command_channel,
						std::string("flash-bench ") + std::to_string(length),
						nullptr,
						reply,
						&data,
						std::string("OK flash-bench: sending ") + std::to_string(length) + " bytes",
						string_value, int_value);

			gettimeofday(&time_now, 0);

			seconds = time_now.tv_sec - time_start.tv_sec;
			useconds = time_now.tv_usec - time_start.tv_usec;
			duration = seconds + (useconds / 1000000.0);
			rate = current * 4.0 / duration;

			if(!option_verbose)
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

		usleep(200000);
		std::cout << std::endl;
	}
}

static void command_image_send_sector(GenericSocket &command_channel,
		int current_sector, const std::string &data,
		unsigned int current_x, unsigned int current_y, unsigned int depth)
{
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
				pixels = data.length() * 8;
				break;
			}

			case(16):
			{
				pixels = data.length() / 2;
				break;
			}

			default:
			{
				throw(std::string("unknown display colour depth"));
			}
		}

		command = (boost::format("display-plot %u %u %u\n") % pixels % current_x % current_y).str();
		process(command_channel, command, &data, reply, nullptr, "display plot success: yes", string_value, int_value);
	}
	else
	{
		unsigned int sectors_written, sectors_erased, sectors_skipped;
		std::string pad;

		pad.assign(0x00, 4096 - data.length());

		write_sector(command_channel, current_sector, data + pad, sectors_written, sectors_erased, sectors_skipped, false);
	}
}

static void command_image(GenericSocket &command_channel, int image_slot, const std::string &filename,
		unsigned int dim_x, unsigned int dim_y, unsigned int depth, int image_timeout)
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

		if(option_verbose)
			std::cout << "image loaded from " << filename << ", " << image.columns() << "x" << image.rows() << ", " << image.magick() << std::endl;

		image.resize(newsize);

		if((image.columns() != dim_x) || (image.rows() != dim_y))
			throw(std::string("image magic resize failed"));

		if(image_slot < 0)
			process(command_channel, std::string("display-freeze ") + std::to_string(10000), nullptr, reply, nullptr,
					"display freeze success: yes", string_value, int_value);

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
							command_image_send_sector(command_channel, current_sector, std::string((const char *)sector_buffer, current_buffer / 8), start_x, start_y, depth);
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
							command_image_send_sector(command_channel, current_sector, std::string((const char *)sector_buffer, current_buffer), start_x, start_y, depth);
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

			command_image_send_sector(command_channel, current_sector, std::string((const char *)sector_buffer, current_buffer), start_x, start_y, depth);
		}

		std::cout << std::endl;

		if(image_slot < 0)
			process(command_channel, std::string("display-freeze ") + std::to_string(0), nullptr, reply, nullptr,
					"display freeze success: yes", string_value, int_value);

		if((image_slot < 0) && (image_timeout > 0))
			process(command_channel, std::string("display-freeze ") + std::to_string(image_timeout), nullptr, reply, nullptr,
					"display freeze success: yes", string_value, int_value);
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

static void cie_spi_write(GenericSocket &command_channel, const std::string &data, const std::string &match)
{
	std::vector<int> int_value;
	std::vector<std::string> string_value;
	std::string reply;

	process(command_channel, data, nullptr, reply, nullptr, match, string_value, int_value);
}

static void cie_uc_cmd_data(GenericSocket &command_channel, bool isdata, unsigned int data_value)
{
	std::string data, reply;
	std::stringstream text;

	text.str("");
	text << "spt 17 8 " << std::hex << std::setfill('0') << std::setw(2) << data_value << " 0 0 0 0";
	data = text.str();

	cie_spi_write(command_channel, "sps", "spi start ok");
	cie_spi_write(command_channel, std::string("iw 1 0 ") + (isdata ? "1" : "0"), std::string("digital output: \\[") + (isdata ? "1" : "0") + "\\]");
	cie_spi_write(command_channel, data, "spi transmit ok");
	cie_spi_write(command_channel, "spf", "spi finish ok");
}

static void cie_uc_cmd(GenericSocket &command_channel, unsigned int cmd)
{
	return(cie_uc_cmd_data(command_channel, false, cmd));
}

static void cie_uc_data(GenericSocket &command_channel, unsigned int data)
{
	return(cie_uc_cmd_data(command_channel, true, data));
}

static void cie_uc_data_string(GenericSocket &command_channel, const std::string valuestring)
{
	cie_spi_write(command_channel, "iw 1 0 1", "digital output: \\[1\\]");
	cie_spi_write(command_channel, "sps", "spi start ok");
	cie_spi_write(command_channel, std::string("spw 8 ") + valuestring, "spi write ok");
	cie_spi_write(command_channel, "spt 17 0 0 0 0 0 0 0", "spi transmit ok");
	cie_spi_write(command_channel, "spf", "spi finish ok");
}

static void command_image_epaper(GenericSocket &command_channel, const std::string &filename)
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

	cie_spi_write(command_channel, "spc 0 0", "spi configure ok");

	cie_uc_cmd(command_channel, 0x04); 	// power on PON, no arguments

	cie_uc_cmd(command_channel, 0x00);	// panel settings PSR, 1 argument
	cie_uc_data(command_channel, 0x0f);	// default

	cie_uc_cmd(command_channel, 0x61); 	// resultion settings TSR, 3 argument
	cie_uc_data(command_channel, 0x68);	// height
	cie_uc_data(command_channel, 0x00);	// width[7]
	cie_uc_data(command_channel, 0xd4);	// width[6-0]

	cie_uc_cmd(command_channel, 0x50); 	// vcom and data interval setting, 1 argument
	cie_uc_data(command_channel, 0xd7);	// default

	try
	{
		Magick::Image image;
		Magick::Geometry newsize(dim_x, dim_y);
		Magick::Color colour;
		newsize.aspect(true);

		if(!filename.length())
			throw(std::string("image epaper: empty file name"));

		image.read(filename);

		if(option_verbose)
			std::cout << "image loaded from " << filename << ", " << image.columns() << "x" << image.rows() << ", " << image.magick() << std::endl;

		image.resize(newsize);

		if((image.columns() != dim_x) || (image.rows() != dim_y))
			throw(std::string("image epaper: image magic resize failed"));

		all_bytes = 0;
		bytes = 0;
		byte = 0;
		bit = 7;
		values = "";
		cie_spi_write(command_channel, "sps", "spi start ok");

		for(x = 0; x < (int)dim_x; x++)
			for(y = 0; y < (int)dim_y; y++)
				dummy_display[x][y] = 0;

		for(layer = 0; layer < 2; layer++)
		{
			cie_uc_cmd(command_channel, layer == 0 ? 0x10 : 0x13); // DTM1 / DTM2

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
							cie_uc_data_string(command_channel, values);
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
				cie_uc_data_string(command_channel, values);
				values = "";
				bytes = 0;
			}

			cie_uc_cmd(command_channel, 0x11); // data stop DST
		}

		cie_uc_cmd(command_channel, 0x12); // display refresh DRF
	}
	catch(const Magick::Error &error)
	{
		throw(std::string("image epaper: load failed: ") + error.what());
	}
	catch(const Magick::Warning &warning)
	{
		std::cout << "image epaper: " << warning.what();
	}

	if(option_verbose)
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

static void command_send(GenericSocket &send_socket, bool dont_wait, std::string args)
{
	std::string reply;
	std::string reply_oob;
	std::string arg;
	size_t current;
	Packet send_packet;
	std::string send_data;
	Packet receive_packet;
	std::string receive_data;
	uint32_t transaction_id;

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

		send_packet.clear();
		send_packet.append_data(arg);

		send_data = send_packet.encapsulate(transaction_id, option_raw, !option_no_provide_checksum, !option_no_request_checksum, option_broadcast_group_mask);

		while(send_data.length() > 0)
			if(!send_socket.send(send_data))
				throw("command send: send failed");

		receive_packet.clear();

		while(!receive_packet.complete())
		{
			receive_data.clear();

			if(!send_socket.receive(receive_data))
				throw("command send: receive failed");

			receive_packet.append_data(receive_data);
		}

		if(!receive_packet.decapsulate(transaction_id, &reply, &reply_oob))
			throw("command send: decapsulate failed");

		std::cout << reply;

		if(reply_oob.length() > 0)
		{
			unsigned int length = 0;

			std::cout << std::endl << reply_oob.length() << " bytes of OOB data:";

			for(const auto &it : reply_oob)
			{
				if((length++ % 20) == 0)
					std::cout << std::endl << "    ";

				std::cout << (boost::format("0x%02x ") % (((unsigned int)it) & 0xff));
			}

			std::cout << std::endl;
		}
		else
			std::cout << std::endl;
	}
}

void command_multicast(GenericSocket &multicast_socket, bool dontwait, const std::string &args)
{
	Packet send_packet(&args, nullptr);
	Packet receive_packet;
	std::string send_data;
	std::string receive_data;
	std::string reply_data;
	std::string packet;
	enum { probes = 8 };
	struct timeval tv_start, tv_now;
	uint64_t start, now;
	struct sockaddr_in remote_host;
	char host_buffer[64];
	char service[64];
	std::string hostname;
	int gai_error;
	std::string reply;
	std::string info;
	std::string line;
	uint32_t host_id;
	typedef struct { int count; std::string hostname; std::string text; } multicast_reply_t;
	typedef std::map<unsigned uint32_t, multicast_reply_t> multicast_replies_t;
	multicast_replies_t multicast_replies;
	int total_replies, total_hosts;
	int run;
	uint32_t transaction_id;

	packet = send_packet.encapsulate(transaction_id, option_raw, !option_no_provide_checksum, !option_no_request_checksum, option_broadcast_group_mask);

	if(dontwait)
	{
		for(run = 0; run < probes; run++)
		{
			send_data = packet;
			multicast_socket.send(send_data, 200);
		}

		return;
	}

	total_replies = total_hosts = 0;

	gettimeofday(&tv_start, nullptr);
	start = (tv_start.tv_sec * 1000000) + tv_start.tv_usec;

	for(run = 0; run < probes; run++)
	{
		gettimeofday(&tv_now, nullptr);
		now = (tv_now.tv_sec * 1000000) + tv_now.tv_usec;

		if(((now - start) / 1000ULL) > 10000)
			break;

		send_data = packet;
		multicast_socket.send(send_data, 0);

		for(;;)
		{
			reply_data.clear();

			if(!multicast_socket.receive(reply_data, 100, &remote_host))
				break;

			receive_packet.clear();
			receive_packet.append_data(reply_data);

			if(!receive_packet.decapsulate(transaction_id, &reply_data, nullptr))
			{
				if(option_verbose)
					std::cout << "multicast: cannot decapsulate" << std::endl;

				continue;
			}

			host_id = ntohl(remote_host.sin_addr.s_addr);

			gai_error = getnameinfo((struct sockaddr *)&remote_host, sizeof(remote_host), host_buffer, sizeof(host_buffer),
					service, sizeof(service), NI_DGRAM | NI_NUMERICSERV | NI_NOFQDN);

			if(gai_error != 0)
			{
				if(option_verbose)
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
				entry.text = reply_data;
				multicast_replies[host_id] = entry;
			}
		}
	}

	for(auto &it : multicast_replies)
	{
		std::string host_id_text;

		host_id_text += std::to_string(((it.first & 0xff000000) >> 24)) + ".";
		host_id_text += std::to_string(((it.first & 0x00ff0000) >> 16)) + ".";
		host_id_text += std::to_string(((it.first & 0x0000ff00) >>  8)) + ".";
		host_id_text += std::to_string(((it.first & 0x000000ff) >>  0));
		std::cout << std::setw(12) << std::left << host_id_text;
		std::cout << " " << std::setw(2) << std::right << it.second.count << " ";
		std::cout << std::setw(12) << std::left << it.second.hostname;
		std::cout << " " << it.second.text;
		std::cout << std::endl;
	}

	std::cout << std::endl << probes << " probes sent, " << total_replies << " replies received, " << total_hosts << " hosts" << std::endl;
}

void commit_ota(GenericSocket &command_channel, unsigned int flash_slot, unsigned int sector, bool reset, bool notemp)
{
	std::string reply;
	std::vector<std::string> string_value;
	std::vector<int> int_value;
	std::string send_data;
	uint32_t transaction_id;

	send_data = (boost::format("flash-select %u %u") % flash_slot % (notemp ? 1 : 0)).str();
	process(command_channel, send_data, nullptr, reply, nullptr, flash_select_expect, string_value, int_value);

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
	send_data = "reset";
	Packet send_packet(&send_data, nullptr);
	send_data = send_packet.encapsulate(transaction_id, option_raw, !option_no_provide_checksum, !option_no_request_checksum, option_broadcast_group_mask);
	command_channel.send(send_data);
	usleep(200000);
	command_channel.drain();
	command_channel.disconnect();
	usleep(1000000);
	command_channel.connect();
	std::cout << "reboot finished" << std::endl;

	process(command_channel, "flash-info", nullptr, reply, nullptr, flash_info_expect, string_value, int_value);

	if(int_value[0] != (int)flash_slot)
		throw(std::string("boot failed, requested slot: ") + std::to_string(flash_slot) + ", active slot: " + std::to_string(int_value[0]));

	if(!notemp)
	{
		std::cout << "boot succeeded, permanently selecting boot slot: " << flash_slot << std::endl;

		send_data = (boost::format("flash-select %u 1") % flash_slot).str();
		process(command_channel, send_data, nullptr, reply, nullptr, flash_select_expect, string_value, int_value);

		if(int_value[0] != (int)flash_slot)
			throw(std::string("flash-select failed, local slot (") + std::to_string(flash_slot) + ") != remote slot (" + std::to_string(int_value[0]) + ")");

		if(int_value[1] != (int)sector)
			throw(std::string("flash-select failed, local sector != remote sector"));

		if(int_value[2] != 1)
			throw(std::string("flash-select failed, local permanent != remote permanent"));
	}

	process(command_channel, "stats", nullptr, reply, nullptr, "\\s*>\\s*firmware\\s*>\\s*date:\\s*([a-zA-Z0-9: ]+).*", string_value, int_value);
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
		unsigned int length;
		bool dont_wait = false;
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
		unsigned int selected;

		options.add_options()
			("info,i",					po::bool_switch(&cmd_info)->implicit_value(true),							"INFO")
			("read,R",					po::bool_switch(&cmd_read)->implicit_value(true),							"READ")
			("verify,V",				po::bool_switch(&cmd_verify)->implicit_value(true),							"VERIFY")
			("simulate,S",				po::bool_switch(&cmd_simulate)->implicit_value(true),						"WRITE simulate")
			("write,W",					po::bool_switch(&cmd_write)->implicit_value(true),							"WRITE")
			("benchmark,B",				po::bool_switch(&cmd_benchmark)->implicit_value(true),						"BENCHMARK")
			("image,I",					po::bool_switch(&cmd_image)->implicit_value(true),							"SEND IMAGE")
			("epaper-image,e",			po::bool_switch(&cmd_image_epaper)->implicit_value(true),					"SEND EPAPER IMAGE (uc8151d connected to host)")
			("broadcast,b",				po::bool_switch(&cmd_broadcast)->implicit_value(true),						"BROADCAST SENDER send broadcast message")
			("multicast,M",				po::bool_switch(&cmd_multicast)->implicit_value(true),						"MULTICAST SENDER send multicast message")
			("host,h",					po::value<std::vector<std::string> >(&host_args)->required(),				"host or broadcast address or multicast group to use")
			("verbose,v",				po::bool_switch(&option_verbose)->implicit_value(true),						"verbose output")
			("tcp,t",					po::bool_switch(&option_use_tcp)->implicit_value(true),						"use TCP instead of UDP")
			("filename,f",				po::value<std::string>(&filename),											"file name")
			("start,s",					po::value<std::string>(&start_string)->default_value("-1"),					"send/receive start address (OTA is default)")
			("length,l",				po::value<std::string>(&length_string)->default_value("0x1000"),			"read length")
			("command-port,p",			po::value<std::string>(&command_port)->default_value("24"),					"command port to connect to")
			("nocommit,n",				po::bool_switch(&nocommit)->implicit_value(true),							"don't commit after writing")
			("noreset,N",				po::bool_switch(&noreset)->implicit_value(true),							"don't reset after commit")
			("notemp,T",				po::bool_switch(&notemp)->implicit_value(true),								"don't commit temporarily, commit to flash")
			("dontwait,d",				po::bool_switch(&dont_wait)->implicit_value(true),							"don't wait for reply on message")
			("image_slot,x",			po::value<int>(&image_slot)->default_value(-1),								"send image to flash slot x instead of frame buffer")
			("image_timeout,y",			po::value<int>(&image_timeout)->default_value(5000),						"freeze frame buffer for y ms after sending")
			("no-provide-checksum,1",	po::bool_switch(&option_no_provide_checksum)->implicit_value(true),			"do not provide checksum")
			("no-request-checksum,2",	po::bool_switch(&option_no_request_checksum)->implicit_value(true),			"do not request checksum")
			("raw,r",					po::bool_switch(&option_raw)->implicit_value(true),							"do not use packet encapsulation")
			("broadcast-groups,g",		po::value<unsigned int>(&option_broadcast_group_mask)->default_value(0),	"select broadcast groups (bitfield)");

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

		GenericSocket command_channel(host, command_port, option_use_tcp, !!cmd_broadcast, !!cmd_multicast);

		if(selected == 0)
			command_send(command_channel, dont_wait, args);
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
					process(command_channel, "flash-info", nullptr, reply, nullptr, flash_info_expect, string_value, int_value);
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

				if(option_verbose)
				{
					std::cout << "flash update available, current slot: " << flash_slot;
					std::cout << ", address[0]: 0x" << std::hex << (flash_address[0] * flash_sector_size) << " (sector " << std::dec << flash_address[0] << ")";
					std::cout << ", address[1]: 0x" << std::hex << (flash_address[1] * flash_sector_size) << " (sector " << std::dec << flash_address[1] << ")";
					std::cout << ", display graphical dimensions: " << dim_x << "x" << dim_y << " px at depth " << depth;
					std::cout << std::endl;
				}

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
					command_read(command_channel, filename, start, length);
				else
					if(cmd_verify)
						command_verify(command_channel, filename, start);
					else
						if(cmd_simulate)
							command_write(command_channel, filename, start, true, false);
						else
							if(cmd_write)
							{
								command_write(command_channel, filename, start, false, otawrite);

								if(otawrite && !nocommit)
									commit_ota(command_channel, flash_slot, start, !noreset, notemp);
							}
							else
								if(cmd_benchmark)
									command_benchmark(command_channel, length);
								else
									if(cmd_image)
										command_image(command_channel, image_slot, filename, dim_x, dim_y, depth, image_timeout);
									else
										if(cmd_image_epaper)
											command_image_epaper(command_channel, filename);
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
	catch(const char *e)
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
