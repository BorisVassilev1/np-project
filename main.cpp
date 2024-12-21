#include <utils.hpp>

#include <algorithm>
#include <chrono>
#include <climits>
#include <csignal>
#include <server.hpp>
#include <socket.hpp>
#include <fcntl.h>
#include <sstream>

std::unique_ptr<HTTPServer> server = nullptr;

void sigintHandler(int) {
	if (server) server->stop();
	exit(0);
}

int main(int argc, char **argv) {
	int threads, port;
	if(argc < 2) {
		threads = std::thread::hardware_concurrency();
	}
	else threads = std::stoi(argv[1]);

	if(argc < 3) {
	    port = 8080;
	} else port = std::stoi(argv[2]);

	server = std::make_unique<HTTPServer>("::1", port, threads);

	server->router.serve("/", "/public");
	server->router.serve("/dir/", "/");
	server->router.get("/asd", [&](SocketStream &ss, std::size_t) { server->router.renderStatus(ss, 500, "BAD"); });

	server->router.get("/wait", [](SocketStream &ss, std::size_t) {
		std::this_thread::sleep_for(std::chrono::seconds(2));
		ss.send(200, "OK", "text/html", "DONT LOOK AT ME");
	});

	server->router.post("/sort", [](SocketStream &ss, std::size_t body_length) {

		std::vector<int> v;
		std::string data;
		data.reserve(body_length);

		while(data.size() < body_length) {
			if(!ss) ss.getSocket().waitREAD(1000);
			ss.clear();
			while(ss && data.size() < body_length)
				data += ss.get();
		}

		std::stringstream datastream(data);
		ss.clear();

		for (;;) {
			int x;
			datastream >> x;
			if (datastream.fail()) {
				ss.status(400, "BAD REQUEST");
				return;
			}
			v.push_back(x);
			if (datastream.eof()) { break; }
		}

		std::sort(v.begin(), v.end());
		std::ostringstream json;
		json << "[";
		for (std::size_t i = 0; i < v.size(); i++) {
			json << (i ? ", " : "") << v[i];
		}
		json << "]";

		ss.clear();
		ss.send(200, "OK", "application/json", json.str());
	});

	server->listen();

	std::cout << "############################################\n"
				 "# Server started.                          #\n"
				 "# Type 'ls' to list clients.               #\n"
				 "# Type 'exit' ot Ctrl-D to stop server.    #\n"
				 "############################################\n"
			  << std::endl;

	std::string line;

	signal(SIGINT, sigintHandler);

	while (std::getline(std::cin, line)) {
		if (line == "exit") break;
		if (line == "ls") { server->listClients(); }
	}

	dbLog(dbg::LOG_INFO, "Stopping server...");
	server->stop();

	return 0;
}
