#include "comp_time_write.hpp"
#include "netlib.h"
#include <print>
#include <iostream>
#include <chrono>
#include <netinet/tcp.h>

std::pair<char *, size_t> load(char *path)
{
	FILE* f = fopen(path, "rb");
	fseek(f, 0, SEEK_END);
	size_t file_size = ftell(f);
	rewind(f);
	char *buffer = (char *)malloc((file_size + 1) * sizeof(char));
	fread(buffer, sizeof(uint8_t), file_size, f);

	return std::pair<char *, size_t> {buffer, file_size};
}

int main()
{
	using clock = std::chrono::system_clock;
	using ms = std::chrono::duration<double, std::milli>;

	char *buffer = (char *)calloc(4096, sizeof(char));
	char *start_buffer = buffer;
	int sockfd = netlib::connect_to_server("127.0.0.1", 8000);
	int yes = 1;
	int result = setsockopt(sockfd,
						IPPROTO_TCP,
						TCP_NODELAY,
						(char *) &yes,
						sizeof(int));
	FILE* f = fopen("../cmake-build-debug/Makefile", "rb");
	fseek(f, 0, SEEK_END);
	size_t file_size = ftell(f);
	rewind(f);
	char *buf = (char *)malloc((4089) * sizeof(char));

	array_with_size<(4096 - 12), char *> name;
	name.array = "Makefile_";
	std::tuple<int,int, array_with_size<(4096 - 12), char *>, int> file_name = {4088, 1, name, 10};
	constexpr std::size_t size = std::tuple_size_v<decltype(file_name)>;
	write_comp_pkt(size, buffer, file_name);
	send(sockfd, start_buffer, 4096, 0);

	std::println("Start!");
	const auto before1 = clock::now();
	while (true)
	{
		buffer = start_buffer;
		memset(buffer, 0, 4096);
		memset(buf, 0, 4089);
		size_t read_size = fread(buf, sizeof(uint8_t), 4088, f);
		if (read_size == 0)
			break;
		int send_size = 4088;

		if (read_size < 4088)
			send_size = read_size;

		array_with_size<4088, const char *> data;
		data.array = buf;
		std::tuple<int, int, array_with_size<4088, const char*>> data_pkt = {send_size, 2, data};
		constexpr std::size_t size_ = std::tuple_size_v<decltype(data_pkt)>;
		write_comp_pkt(size_, buffer, data_pkt);
		send(sockfd, start_buffer, 4096, 0);

		if (read_size < 4088)
			break;
	}
	const ms duration1 = clock::now() - before1;
	std::cout << "It took " << duration1.count() << "ms" << std::endl;
	std::println("Done!");
	return  0;
}