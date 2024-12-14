#pragma once

#include <netinet/in.h>
#include <mutex>
#include <shared_mutex>
#include <thread>

#include <router.hpp>
#include <socket.hpp>

class TCPServer {
   public:
	TCPServer(const std::string &ip = "::1", short port = 8080, int num_threads = std::thread::hardware_concurrency());
	~TCPServer();

	virtual void handleRequest(Socket &) = 0;
	void		 listen();

	void stop();
	void listClients();

   private:
	struct ClientData {
		ClientData(const ClientData &)			  = delete;
		ClientData &operator=(const ClientData &) = delete;
		ClientData(ClientData &&)				  = default;
		ClientData &operator=(ClientData &&)	  = default;
		ClientData(Socket &&s) : socket(std::move(s)), stream(socket) {}

		Socket		 socket;
		SocketStream stream;
	};
	using ClientData_ptr = std::unique_ptr<ClientData>;

	int									m_socket, m_epollFD;
	sockaddr_in6						m_address;
	std::unordered_map<int, ClientData> m_clients;
	std::mutex							m_mutex;
	std::atomic_flag					m_running = 1;
	std::vector<std::thread>			m_workers;
	unsigned int						m_numThreads;
};

class HTTPServer : public TCPServer {
   public:
	using TCPServer::TCPServer;

	virtual void handleRequest(Socket &) override;

	Router router;
};
