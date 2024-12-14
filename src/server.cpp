#include <unistd.h>
#include <csignal>
#include <cstring>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>

#include <server.hpp>
#include <socket.hpp>
#include <sstream>
#include <thread>
#include "router.hpp"

TCPServer::TCPServer(const std::string &ip, short port, int threads) {
	m_numThreads = threads;
	m_socket = socket(AF_INET6, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if (m_socket < 0) { throw std::runtime_error("failed to initialize socket"); }

	m_address.sin6_family = AF_INET6;
	inet_pton(AF_INET6, ip.c_str(), &m_address.sin6_addr);
	m_address.sin6_port		= htons(port);
	m_address.sin6_flowinfo = 0;
	m_address.sin6_scope_id = 0;

	int on = 1;
	setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

	int res;
	res = bind(m_socket, (struct sockaddr *)&m_address, sizeof(m_address));
	std::cout << "binding to: " << m_address << std::endl;
	if (res < 0) { throw std::runtime_error(std::string("cannot bind: ") + strerror(errno)); }

	m_epollFD = epoll_create1(0);
	if (m_epollFD < 0) { throw std::runtime_error(std::string("cannot create epoll: ") + strerror(errno)); }

	epoll_event event;
	event.events  = EPOLLIN | EPOLLET;
	event.data.fd = m_socket;
	if (epoll_ctl(m_epollFD, EPOLL_CTL_ADD, m_socket, &event) < 0) {
		throw std::runtime_error(std::string("cannot add socket to epoll: ") + strerror(errno));
		close(m_epollFD);
	}
	signal(SIGPIPE, SIG_IGN);
}

void TCPServer::listen() {
	if (::listen(m_socket, 5) < 0) { throw std::runtime_error(std::string("cannot listen: ") + strerror(errno)); }

	auto remove = [this](auto it) {
		epoll_event event;
		event.data.fd = it->second.socket;
		epoll_ctl(m_epollFD, EPOLL_CTL_DEL, int(it->second.socket), &event);

		m_clients.erase(it);
	};

	auto worker = [this, remove]() {
		epoll_event event;
		while (m_running.test()) {
			int numEvents = epoll_wait(m_epollFD, &event, 1, 500);
			if (numEvents == -1) {
				throw std::runtime_error(std::string("epoll_wait failed: ") + strerror(errno));
				break;
			}
			if (!numEvents) continue;

			// Client disconnected
			if (event.events & EPOLLRDHUP) {
				std::lock_guard lock(m_mutex);
				auto			it = m_clients.find(event.data.fd);
				if (it != m_clients.end()) {
					std::cout << "Client " << it->second.socket.getAddr() << " disconnected." << std::endl;
					remove(it);
				}
				continue;
			}

			if (event.data.fd == m_socket) {
				// Accept new client connection
				sockaddr_in6 client;
				socklen_t	 clilen = sizeof(client);

				Socket socket = accept4(m_socket, (sockaddr *)&client, &clilen, SOCK_NONBLOCK);
				if (socket < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
				if (socket < 0) { throw std::runtime_error(std::strerror(errno)); }
				socket.setAddr(client);

				// Add client socket to epoll
				event.events  = EPOLLIN | EPOLLONESHOT | EPOLLRDHUP | EPOLLET;
				event.data.fd = socket;
				if (epoll_ctl(m_epollFD, EPOLL_CTL_ADD, socket, &event) == -1) {
					std::cerr << "Failed to add client socket to epoll instance." << std::endl;
					close(socket);
					continue;
				}

				// Add client to client list
				ClientData *clientData = nullptr;
				{
					std::lock_guard lock(m_mutex);
					int				sock_fd = int(socket);
					auto [it, inserted]		= m_clients.emplace(int(socket), ClientData(std::move(socket)));
					if (!inserted) {
						std::cerr << "Failed to add client to client list." << std::endl;
						close(sock_fd);
						continue;
					}
					clientData		  = &it->second;
					it->second.stream = SocketStream(it->second.socket);
					std::cout << "Accepted new client connection from " << it->second.socket.getAddr() << std::endl;
					// check for empty stream
					it->second.stream.clear();
					it->second.stream.peek();
					if (!it->second.stream) {
						std::cout << "Client " << it->second.socket.getAddr() << " disconnected." << std::endl;
						remove(it);
						continue;
					}
				}
				handleRequest(clientData->socket);

				epoll_ctl(m_epollFD, EPOLL_CTL_MOD, event.data.fd, &event);
			} else {
				// Handle client request
				ClientData *clientData = nullptr;
				{
					std::lock_guard lock(m_mutex);
					// Handle client request
					auto it = m_clients.find(event.data.fd);
					if (it == m_clients.end()) {
						int fd = event.data.fd;
						epoll_ctl(m_epollFD, EPOLL_CTL_DEL, fd, &event);
						close(fd);
						continue;
					}
					clientData = &it->second;
					// check for empty stream, aka client disconnected
					it->second.stream.clear();
					it->second.stream.peek();
					if (!it->second.stream) {
						std::cout << "Client " << it->second.socket.getAddr() << " disconnected." << std::endl;
						remove(it);
						continue;
					}
				}

				handleRequest(clientData->socket);
				epoll_ctl(m_epollFD, EPOLL_CTL_MOD, clientData->socket, &event);
			}
		}
		std::unique_lock lock(m_mutex);
		std::cout << "Worker thread stopped." << std::endl;
	};

	for (unsigned int i = 0; i < m_numThreads; i++) {
		m_workers.emplace_back(worker);
	}
}

TCPServer::~TCPServer() {
	close(m_socket);
	close(m_epollFD);
}

void TCPServer::stop() {
	m_running.clear();
	for (auto &it : m_workers) {
		it.join();
	}
}

void TCPServer::listClients() {
	std::lock_guard lock(m_mutex);
	std::cout << "Clients: " << m_clients.size() << "{";
	for (auto &it : m_clients) {
		std::cout << it.second.socket.getAddr() << ", ";
	}
	std::cout << "}" << std::endl;
}

void HTTPServer::handleRequest(Socket &socket) {
	SocketStream stream(socket);

	std::string line;
	std::getline(stream, line);
	if (line.empty()) return;
	std::cout << socket.getAddr() << " -> " << line << std::endl;

	std::istringstream	ss(line);
	Router::RequestType type;
	{
		std::string type_s;
		ss >> type_s;
		type = Router::RequestType::fromString(type_s);
	}
	std::string path;
	ss >> path;

	while (std::getline(stream, line)) {
		if (line == "\r") break;
	}

	router.handleRequest(type, path, stream);
}
