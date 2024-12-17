#include <utils.hpp>
#include <mutex>

std::mutex &dbg::getMutex() {
	static std::mutex m;
	return m;
}
