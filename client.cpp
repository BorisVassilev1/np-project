#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <charconv>
#include <thread>
#include "src/socket.hpp"

int received = 0;

void read_data(SocketStream &stream) {
	std::string response;
	std::size_t len = 0;
	while (stream.good()) {
		std::getline(stream, response);
		if (response.empty()) return;
		if (response.starts_with("Content-Length:")) {
			std::string_view length = std::string_view(response).substr(16);
			std::from_chars(length.begin(), length.end(), len);
		}
		if (response == "\r") break;
		// std::cout << response << std::endl;
	}
	++received;
	if (len) {
		char *buffer = new char[len];
		stream.read(buffer, len);
		std::cout.write(buffer, std::min(len, 40ul));
		std::cout << std::endl;
		delete[] buffer;
	} else return;

	// std::cout << std::endl << std::endl;
}

Socket createConnection(const std::string &ip, short port) {
	int sock = socket(AF_INET6, SOCK_STREAM, 0);
	if (sock == -1) {
		std::cerr << "Error creating socket" << std::endl;
		return 1;
	}
	// std::cout << "Socket created" << std::endl;
	struct sockaddr_in6 addr;
	addr.sin6_family = AF_INET6;
	addr.sin6_port	 = htons(port);
	inet_pton(AF_INET6, ip.c_str(), &addr.sin6_addr);
	if (connect(sock, (struct sockaddr *)&addr, sizeof(addr))) {
		std::cerr << "Error connecting to server: " << strerror(errno) << std::endl;
		return 1;
	}
	// std::cout << "Connected to server" << std::endl;
	return sock;
}

int main(int argc, char **argv) {
	if (argc < 3) {
		std::cerr << "Usage: " << argv[0] << " <num_connections> <num_requests>" << std::endl;
		return 1;
	}
	int num_connections = std::stoi(argv[1]);
	int num_requests	= std::stoi(argv[2]);
	int index			= argc == 4 ? std::stoi(argv[3]) : 0;

	std::vector<Socket>						   sockets;
	std::vector<std::shared_ptr<SocketStream>> streams;

	for (int i = 0; i < num_connections; i++) {
		auto &s = sockets.emplace_back(createConnection("::1", 8080));
		streams.emplace_back(std::make_shared<SocketStream>(s));
	}

	std::string requests[3] = {"GET /wait HTTP/1.1\r\nHost: localhost\r\n\r\n",
							   "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n"};

	int num_size = 10000;
	std::stringstream ss;
	for (int i = 0; i < num_size; ++i) {
		ss << (i ? " " : "") << (rand() % num_size);
	}

	requests[2] =
		"POST /sort HTTP/1.1\r\nHost: localhost\r\n"
		"Content-Length: " +
		std::to_string(ss.str().size()) + "\r\n\r\n" + ss.str() + "\n";

	std::cout << "Packet size: " << requests[index].size() << std::endl;

	for (int j = 0; j < num_requests; j++) {
		for (auto &p : streams) {
			auto &stream = *p;
			stream << requests[index];
			stream.flush();
			// std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
	}

	for (int i = num_connections; --i >= 0;) {
		for (int j = 0; j < num_requests; j++)
			read_data(*streams[i]);
		streams.pop_back();
		sockets.pop_back();
	}

	std::cout << std::endl;
	std::cout << "Sent " << num_connections * num_requests << " requests" << std::endl;
	std::cout << "Received " << received << " responses" << std::endl;

	return 0;
}
