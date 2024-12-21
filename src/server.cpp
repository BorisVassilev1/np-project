#include <unistd.h>
#include <charconv>
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
#include "utils.hpp"

static void signalHandler(int sig) { dbLog(dbg::LOG_WARNING, "Caught signal: ", sig); }

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

	dbLog(dbg::LOG_INFO, "Listening on ", m_address);

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
			// dbLog(dbg::LOG_DEBUG, "Worker thread ", id, " got event.");

			// Client disconnected
			if (event.events & EPOLLRDHUP) {
				std::lock_guard lock(m_mutex);
				auto			it = m_clients.find(event.data.fd);
				if (it != m_clients.end()) {
					dbLog(dbg::LOG_DEBUG, "Client ", it->second->socket.getAddr(), " disconnected.");
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
					dbLog(dbg::LOG_ERROR, "Failed to add client socket to epoll instance: ", strerror(errno));
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
						dbLog(dbg::LOG_ERROR, "Failed to add client to client list: ", strerror(errno));
						close(sock_fd);
						continue;
					}
					clientData = it->second;
					dbLog(dbg::LOG_DEBUG, "Accepted new client connection from ", it->second->socket.getAddr());
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

				int k = 0;
				if (clientData->lock.compare_exchange_strong(k, 1)) {
					clientData->stream.clear();
					while (clientData->lock.exchange(!!clientData->stream)) {
						handleRequest(clientData->stream);
					}
				}
				//{
				//	std::lock_guard lock(clientData->spinlock);
				//	while (clientData->stream) {
				//		clientData->stream.clear();
				//		handleRequest(clientData->stream);
				//	}
				//}
			}
		}
		std::unique_lock lock(m_mutex);
		dbLog(dbg::LOG_DEBUG, "Worker thread ", id, " stopped.");
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
	std::lock_guard lock1(m_mutex);
	std::lock_guard lock2(dbg::getMutex());

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
	dbLog(dbg::LOG_INFO, socket.getAddr(), " -> ", line);

	std::istringstream	ss(line);
	Router::RequestType type;
	{
		std::string type_s;
		ss >> type_s;
		type = Router::RequestType::fromString(type_s);
	}
	std::string path;
	ss >> path;

	std::size_t body_len = 0;
	while (std::getline(stream, line)) {
		if (line.starts_with("Content-Length:")) {
			std::string_view length = std::string_view(line).substr(16);
			std::from_chars(length.begin(), length.end(), body_len);
		}
		if (line == "\r") break;
	}

	router.handleRequest(type, path, stream, body_len);
}
