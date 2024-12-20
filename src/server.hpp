#pragma once

#include <netinet/in.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

#include <router.hpp>
#include <socket.hpp>
#include "utils.hpp"

#if !defined(__linux__)
	#error "This example is for Linux only"
#endif

class TCPServer {
   public:
	TCPServer(const std::string &ip = "::1", short port = 8080, int num_threads = std::thread::hardware_concurrency());
	virtual ~TCPServer();

	virtual void handleRequest(SocketStream &) = 0;
	void		 listen();

	void stop();
	void listClients();

   private:
	struct ClientData {
		ClientData(const ClientData &)			  = delete;
		ClientData &operator=(const ClientData &) = delete;
		ClientData(Socket &&s) : socket(std::move(s)), stream(socket) {}

		Socket		 socket;
		SocketStream stream;
		std::atomic_int lock; // 0 - free, 1 - locked
		SpinLock		 spinlock;
	};
	using ClientData_ptr = std::unique_ptr<ClientData>;

	int									m_socket, m_epollFD;
	sockaddr_in6						m_address;
	std::unordered_map<int, std::shared_ptr<ClientData>> m_clients;
	std::mutex							m_mutex;
	std::atomic_flag					m_running = 1;
	std::vector<std::thread>			m_workers;
	unsigned int						m_numThreads;

	std::atomic_int m_occup[100] = {0};
};

class HTTPServer : public TCPServer {
   public:
	using TCPServer::TCPServer;

	virtual void handleRequest(SocketStream &) override;

	Router router;
};
