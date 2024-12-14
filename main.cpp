#include <algorithm>
#include <climits>
#include <server.hpp>
#include <socket.hpp>
#include <fcntl.h>
#include <sstream>

int main() {
	HTTPServer server("::1", 8080);
	server.router.serve("/", "/public");
	server.router.serve("/dir/", "/");
	server.router.get("/asd", [&](SocketStream &ss) { server.router.renderStatus(ss, 500, "BAD"); });

	server.router.get("/wait", [](SocketStream &ss) {
		std::this_thread::sleep_for(std::chrono::seconds(5));
		ss.send(200, "OK", "text/html", "DONT LOOK AT ME");
	});

	server.router.post("/sort", [](SocketStream &ss) {
		std::vector<int> v;
		std::string		 data;
		std::getline(ss, data, '\n');
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

		ss.send(200, "OK", "application/json", json.str());
	});

	server.listen();

	std::cout << "############################################\n"
				 "# Server started.                          #\n"
				 "# Type 'ls' to list clients.               #\n"
				 "# Type 'exit' ot Ctrl-D to stop server.    #\n"
				 "############################################\n"
			  << std::endl;

	std::string line;
	while (std::getline(std::cin, line)) {
		if (line == "exit") break;
		if (line == "ls") { server.listClients(); }
	}

	std::cout << "Stopping server..." << std::endl;
	server.stop();

	return 0;
}
