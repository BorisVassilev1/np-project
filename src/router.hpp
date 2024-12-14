#pragma once

#include <functional>
#include <sstream>
#include <string>
#include <socket.hpp>
#include <unordered_map>

#include <fcntl.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdio.h>

#include "utils.hpp"

class Router {
   public:
	using Handler = std::function<void(SocketStream &)>;

	class RequestType {
	   public:
		enum Value : uint8_t { GET, HEAD, POST, PUT, DELETE, CONNECT, OPTIONS, TRACE, PATCH };

		RequestType() = default;
		RequestType(Value v) : value(v) {}
		RequestType(const std::string &s) : value(fromString(s)) {}
		operator Value() const { return value; }

		std::string toString() const { return toString(*this); }

		static RequestType fromString(const std::string &s) {
			if (s == "GET") return GET;
			if (s == "HEAD") return HEAD;
			if (s == "POST") return POST;
			if (s == "PUT") return PUT;
			if (s == "DELETE") return DELETE;
			if (s == "CONNECT") return CONNECT;
			if (s == "OPTIONS") return OPTIONS;
			if (s == "TRACE") return TRACE;
			if (s == "PATCH") return PATCH;
			return GET;
		}

		static std::string toString(RequestType t) {
			switch (t) {
				case GET: return "GET";
				case HEAD: return "HEAD";
				case POST: return "POST";
				case PUT: return "PUT";
				case DELETE: return "DELETE";
				case CONNECT: return "CONNECT";
				case OPTIONS: return "OPTIONS";
				case TRACE: return "TRACE";
				case PATCH: return "PATCH";
			}
			return "GET";
		}

	   private:
		Value value;
	};

	void addRoute(RequestType t, const std::string &path, const Handler &h) { map.insert({{path, t}, h}); }

	void get(const std::string &path, const Handler &h) { addRoute(RequestType::GET, path, h); }
	void post(const std::string &path, const Handler &h) { addRoute(RequestType::POST, path, h); }
	void put(const std::string &path, const Handler &h) { addRoute(RequestType::PUT, path, h); }
	void del(const std::string &path, const Handler &h) { addRoute(RequestType::DELETE, path, h); }

	void serve(const std::string &web_path, const std::string &path) { served.insert({web_path, path}); }

	void handleRequest(RequestType t, std::string &path, SocketStream &s) {
		std::size_t i = path.size() - 1;

		auto handler = map.find({path, t});
		if (handler != map.end()) {
			handler->second(s);
			return;
		}

		bool match = false;

		do {
			std::string_view v = std::string_view(path).substr(0, i + 1);

			auto j = served.find(v);
			if (j != served.end()) {
				handleFileRequest(s, j->second, path.substr(i + 1));
				match = true;
				break;
			}

		} while ((i = path.rfind('/', std::max(i - 1, 0ul))) != std::string::npos);

		if (!match) {
			renderStatus(s, 404, "Not Found");
			return;
		}
	}

	void renderStatus(SocketStream &ss, int status, const std::string &msg) {
		int res = sendFile(ss, "./fixed/" + std::to_string(status) + ".html", status, msg);
		if (res) ss.status(status, msg);
	}

   private:
	int serveDirList(SocketStream &ss, const std::string &path) {
		DIR			  *d;
		struct dirent *file;
		d = opendir(path.c_str());
		if (!d) {
			std::cerr << "cannot open dir: " << path << std::endl;
			return 1;
		}

		std::stringstream list;

		if (d) {
			while ((file = readdir(d)) != NULL) {
				std::string name = file->d_name;
				if (file->d_type == DT_DIR) {
					list << "<a href = \"./" << name << "/\">" << name << "/</a><br>";
				} else if (file->d_type == DT_REG) {
					list << "<a href = \"./" << name << "\">" << name << "</a><br>";
				} else {
					list << name << "<br>";
				}
			}
			closedir(d);
		}

		ss.send(200, "OK", "text/html", list);
		return 0;
	}

	int sendFile(SocketStream &ss, const std::string &path, int status = 200, const std::string_view &msg = "OK") {
		int fd = open(path.c_str(), O_RDONLY);
		if (fd < 0) { return 1; }

		struct stat statbuf;
		fstat(fd, &statbuf);
		if (S_ISDIR(statbuf.st_mode)) {
			close(fd);
			return 1;
		}

		ss << "HTTP/1.1 " << status << " " << msg
		   << "\r\n"
			  "Content-Type: ";

		if (path.ends_with(".html")) ss << "text/html";
		else if (path.ends_with(".css")) ss << "text/css";
		else if (path.ends_with(".js")) ss << "text/javascript";
		else if (path.ends_with(".png")) ss << "image/png";
		else if (path.ends_with(".jpg")) ss << "image/jpeg";
		else if (path.ends_with(".jpeg")) ss << "image/jpeg";
		else if (path.ends_with(".gif")) ss << "image/gif";
		else if (path.ends_with(".svg")) ss << "image/svg+xml";
		else if (path.ends_with(".ico")) ss << "image/x-icon";
		else if (path.ends_with(".json")) ss << "application/json";
		else ss << "text/plain";

		ss << ";charset=utf-8\r\n"
			  "Content-Length: "
		   << statbuf.st_size << "\r\n\r\n"
		   << std::flush;

		int res = 1;
		while (res > 0) {
			res = sendfile(ss.getSocket(), fd, 0, 409600);
		}
		close(fd);
		return 0;
	}

	void handleFileRequest(SocketStream &ss, const std::string &cwd, const std::string &path) {
		std::string local_path = '.' + cwd +"/"+ path;

		if (path == "" || path.back() == '/') {
			int res = sendFile(ss, local_path + "/index.html");
			if (res) { res = serveDirList(ss, local_path); }
			if (res) { renderStatus(ss, 404, "Not Found"); }
			return;
		}

		int res = sendFile(ss, local_path);
		if (res) { renderStatus(ss, 404, "Not Found"); }
	}

	std::unordered_map<std::pair<std::string, RequestType>, Handler>					  map;
	std::unordered_map<std::string, std::string, std::hash<std::string>, std::equal_to<>> served;
};

template <>
struct std::hash<Router::RequestType> {
	std::size_t operator()(Router::RequestType t) const { return std::hash<uint8_t>()(t); }
};
