#pragma once

#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <streambuf>
#include <istream>
#include <netinet/in.h>
#include <unistd.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <poll.h>

#include <utils.hpp>

#define BUFFER_SIZE 4096

inline void waitREAD(int socket, int timeout = -1) {
	struct pollfd pfd;
	pfd.fd		= socket;
	pfd.events	= POLLIN;
	pfd.revents = 0;
	if (poll(&pfd, 1, timeout) == -1) { throw std::runtime_error("poll failed"); }
}

inline void waitWRITE(int socket, int timeout = -1) {
	struct pollfd pfd;
	pfd.fd		= socket;
	pfd.events	= POLLOUT;
	pfd.revents = 0;
	if (poll(&pfd, 1, timeout) == -1) { throw std::runtime_error("poll failed"); }
}

class SocketBuffer : public std::streambuf {
   public:
	explicit SocketBuffer(int socket_fd) : socket_fd(socket_fd) {
		setg(buffer, buffer, buffer);

		setp(output_buffer, output_buffer + BUFFER_SIZE);
	}
	~SocketBuffer() override { sync(); }

   protected:
	int_type underflow() override {
		if (gptr() == egptr()) {
			ssize_t bytes_read = read(socket_fd, buffer, BUFFER_SIZE);
			if (bytes_read <= 0) { throw std::runtime_error("failed to read from socket"); }
			// if (bytes_read == 0) { return traits_type::eof(); }

			setg(buffer, buffer, buffer + bytes_read);
		}
		return traits_type::to_int_type(*gptr());
	}

	int_type overflow(int_type ch) override {
		if (sync() == -1) { return traits_type::eof(); }
		if (ch != traits_type::eof()) {
			*pptr() = traits_type::to_char_type(ch);
			pbump(1);
		}
		return traits_type::not_eof(ch);
	}

	int sync() override {
		ssize_t bytes_to_write = pptr() - pbase();
		if (bytes_to_write > 0) {
			ssize_t bytes_written = send(socket_fd, output_buffer, bytes_to_write, 0);
			if (bytes_written <= 0) {
				waitWRITE(socket_fd);
				bytes_written = send(socket_fd, output_buffer, bytes_to_write, 0);
			}

			if (bytes_written < 0) {
				dbLog(dbg::LOG_WARNING, "Failed to write to socket: ", strerror(errno));
				return -1;
			}
			pbump(-bytes_written);
		}
		return 0;
	}

   private:
	int	 socket_fd;
	char buffer[BUFFER_SIZE];
	char output_buffer[BUFFER_SIZE];
};

class Socket {
   public:
	Socket(const Socket &)			  = delete;
	Socket &operator=(const Socket &) = delete;
	Socket(Socket &&s) {
		this->socket = s.socket;
		this->addr	 = s.addr;
		s.socket	 = -1;
	}
	Socket &operator=(Socket &&s) {
		this->socket = s.socket;
		this->addr	 = s.addr;
		s.socket	 = -1;
		return *this;
	}

	Socket(int socket) : socket(socket) {}
	~Socket() {
		if (this->socket != -1) close(this->socket);
	}

	operator int() const { return this->socket; }

	const sockaddr_in6 &getAddr() const { return this->addr; }
	void				setAddr(const sockaddr_in6 &addr) { this->addr = addr; }

	void waitREAD(int timeout = -1) const { ::waitREAD(this->socket, timeout); }

	void waitWRITE(int timeout = -1) const { ::waitWRITE(this->socket, timeout); }

   private:
	int			 socket = 0;
	sockaddr_in6 addr;
};

class SocketStream : public std::iostream {
   public:
	SocketStream(const Socket &s) : std::iostream(&buffer), buffer((int)s), socket(&s) {}
	// SocketStream(SocketStream &&s) : std::iostream(&s.buffer), buffer(std::move(s.buffer)), socket(s.socket) {
	//	s.socket = nullptr;
	// }

	SocketStream(int socket) : std::iostream(&buffer), buffer(socket), socket(nullptr) {}

	~SocketStream() override { this->flush(); }
	SocketStream &operator=(SocketStream &&s) {
		this->buffer = std::move(s.buffer);
		this->socket = s.socket;
		s.socket	 = nullptr;
		return *this;
	}
	const Socket &getSocket() { return *socket; }

	void status(int status, const std::string &msg) {
		if (status < 0) { throw std::runtime_error("invalid status code"); }
		this->clear();
		(*this) << "HTTP/1.1 " << status << ' ' << msg
				<< "\r\n"
				   "Content-Type: text/html\r\n"
				   "Content-Length: "
				<< msg.size() << "\r\n\r\n"
				<< msg << std::flush;
	}

	void send(int status, const std::string &msg, const std::string &content_type, const std::string &content) {
		if (status < 0) { throw std::runtime_error("invalid status code"); }
		this->clear();
		(*this) << "HTTP/1.1 " << status << ' ' << msg
				<< "\r\n"
				   "Content-Type: "
				<< content_type
				<< "\r\n"
				   "Content-Length: "
				<< content.size() << "\r\n\r\n"
				<< content << std::flush;
	}

	void send(int status, const std::string &msg, const std::string &content_type, std::istream &content) {
		if (status < 0) { throw std::runtime_error("invalid status code"); }
		this->clear();
		(*this) << "HTTP/1.1 " << status << ' ' << msg
				<< "\r\n"
				   "Content-Type: "
				<< content_type
				<< "\r\n"
				   "Content-Length: ";
		content.seekg(0, std::ios::end);
		(*this) << content.tellg() << "\r\n\r\n";
		content.seekg(0, std::ios::beg);
		(*this) << content.rdbuf() << std::flush;
	}

   private:
	SocketBuffer  buffer;	  // Our custom stream buffer
	const Socket *socket;
};

inline std::ostream &operator<<(std::ostream &out, const sockaddr_in6 &addr) {
	char ip[64];
	if (inet_ntop(AF_INET6, &addr.sin6_addr, ip, sizeof(ip)) == nullptr) {
		throw std::runtime_error("cannot convert ip to string");
	}
	out << "[" << ip << "]:" << ntohs(addr.sin6_port);

	return out;
}

inline std::ostream &operator<<(std::ostream &out, const sockaddr_in &addr) {
	char ip[64];
	if (inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip)) == nullptr) {
		throw std::runtime_error("cannot convert ip to string");
	}
	out << ip << ":" << ntohs(addr.sin_port);

	return out;
}
