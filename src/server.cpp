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

static void signalHandler(int sig) { std::cout << "Caught signal " << sig << std::endl; }

TCPServer::TCPServer(const std::string &ip, short port, int threads) {
	m_numThreads = threads;
	m_socket	 = socket(AF_INET6, SOCK_STREAM | SOCK_NONBLOCK, 0);
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
	if (res < 0) { throw std::runtime_error(std::string("cannot bind: ") + strerror(errno)); }

	m_epollFD = epoll_create1(0);
	if (m_epollFD < 0) { throw std::runtime_error(std::string("cannot create epoll: ") + strerror(errno)); }

	epoll_event event;
	event.events  = EPOLLIN;	 // | EPOLLET;
	event.data.fd = m_socket;
	if (epoll_ctl(m_epollFD, EPOLL_CTL_ADD, m_socket, &event) < 0) {
		throw std::runtime_error(std::string("cannot add socket to epoll: ") + strerror(errno));
		close(m_epollFD);
	}
	if (signal(SIGPIPE, signalHandler) == SIG_ERR) {
		throw std::runtime_error(std::string("cannot ignore SIGPIPE: ") + strerror(errno));
	}
}

void TCPServer::listen() {
	if (::listen(m_socket, 10) < 0) { throw std::runtime_error(std::string("cannot listen: ") + strerror(errno)); }

	std::cout << "Listening on " << m_address << std::endl;

	auto remove = [this](auto it) {
		epoll_event event;
		event.data.fd = it->second->socket;
		epoll_ctl(m_epollFD, EPOLL_CTL_DEL, int(it->second->socket), &event);

		m_clients.erase(it);
	};

	auto worker = [this, remove](int id) {
		// set signal mask to ignore SIGPIPE
		sigset_t mask;
		sigemptyset(&mask);
		sigaddset(&mask, SIGPIPE);
		if (pthread_sigmask(SIG_BLOCK, &mask, nullptr) != 0) {
			throw std::runtime_error(std::string("cannot set signal mask: ") + strerror(errno));
		}

		epoll_event event;
		while (m_running.test()) {
			// wait for client interaction or new connection
			m_occup[id].store(0);
			int numEvents = epoll_wait(m_epollFD, &event, 1, 1000);
			m_occup[id].store(1);
			if (numEvents == -1) {
				throw std::runtime_error(std::string("epoll_wait failed: ") + strerror(errno));
				break;
			}
			if (!numEvents) continue;
			std::cout << "Worker thread started." << id << std::endl;

			// Client disconnected
			if (event.events & EPOLLRDHUP) {
				std::lock_guard lock(m_mutex);
				auto			it = m_clients.find(event.data.fd);
				if (it != m_clients.end()) {
					std::cout << "Client " << it->second->socket.getAddr() << " disconnected." << std::endl;
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
				event.events  = EPOLLIN | EPOLLRDHUP | EPOLLET;
				event.data.fd = socket;
				if (epoll_ctl(m_epollFD, EPOLL_CTL_ADD, socket, &event) == -1) {
					std::cerr << "Failed to add client socket to epoll instance." << strerror(errno) << std::endl;
					close(socket);
					continue;
				}

				// Add client to client list
				std::shared_ptr<ClientData> clientData = nullptr;
				{
					std::lock_guard lock(m_mutex);
					int				sock_fd = int(socket);
					auto [it, inserted] =
						m_clients.emplace(int(socket), std::make_shared<ClientData>(std::move(socket)));
					if (!inserted) {
						std::cerr << "Failed to add client to client list." << std::endl;
						close(sock_fd);
						continue;
					}
					clientData = it->second;
					std::cout << "Accepted new client connection from " << it->second->socket.getAddr() << std::endl;
				}

			} else {
				// Handle client request
				std::shared_ptr<ClientData> clientData = nullptr;
				{
					std::lock_guard lock(m_mutex);
					// Handle client request
					auto it = m_clients.find(event.data.fd);
					if (it == m_clients.end()) {
						int fd = event.data.fd;
						epoll_ctl(m_epollFD, EPOLL_CTL_DEL, fd, &event);
						continue;
					}
					clientData = it->second;
				}

				std::lock_guard lock(clientData->mutex);
				clientData->stream.clear();
				handleRequest(clientData->stream);
			}
		}
		std::unique_lock lock(m_mutex);
		std::cout << "Worker thread stopped." << std::endl;
	};

	for (unsigned int i = 0; i < m_numThreads; i++) {
		m_workers.emplace_back(worker, i);
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
	std::cout << "Clients count: " << m_clients.size() << " | thread occupancy: ";
	for (std::size_t i = 0; i < m_numThreads; i++) {
		std::cout << m_occup[i] << " ";
	}
	std::cout << std::endl;
}

void HTTPServer::handleRequest(SocketStream &stream) {
	const Socket &socket = stream.getSocket();

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
