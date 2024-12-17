#pragma once

#include <atomic>
#include <functional>
#include <string_view>
#include <string>
#include <iostream>

template <int N, typename... Ts>
using NthTypeOf = typename std::tuple_element<N, std::tuple<Ts...>>::type;

template <class... Args>
struct std::hash<std::tuple<Args...>> {
	std::size_t operator()(const std::tuple<Args...> &t) const {
		return [&]<std::size_t... p>(std::index_sequence<p...>) {
			return ((std::hash<NthTypeOf<p, Args...>>{}(std::get<p>(t))) ^ ...);
		}(std::make_index_sequence<std::tuple_size_v<std::tuple<Args...>>>{});
	}
};

template <class A, class B>
struct std::hash<std::pair<A, B>> {
	std::size_t operator()(const std::pair<A, B> &p) const {
		return std::hash<A>{}(p.first) ^ std::hash<B>{}(p.second);
	}
};

template <>
struct std::hash<std::string> {
	using is_transparent = void;
	[[nodiscard]] size_t operator()(const char *txt) const { return std::hash<std::string_view>{}(txt); }
	[[nodiscard]] size_t operator()(std::string_view txt) const { return std::hash<std::string_view>{}(txt); }
	[[nodiscard]] size_t operator()(const std::string &txt) const { return std::hash<std::string_view>{}(txt.c_str()); }
};

/**
 * @brief SpinLock, copy pasta from lectures
 */
struct SpinLock {
	std::atomic_flag flag;
	void			 lock() {
		while (flag.test_and_set())
			;
	}

	void unlock() { flag.clear(); }

	bool tryLock() { return !flag.test_and_set(); }
};

namespace dbg {
/**
 *
 * @brief Log levels
 */
enum {
	LOG_INFO	= (0),
	LOG_DEBUG	= (1),
	LOG_WARNING = (2),
	LOG_ERROR	= (3),
};

#define COLOR_RESET	 "\033[0m"
#define COLOR_RED	 "\x1B[0;91m"
#define COLOR_GREEN	 "\x1B[0;92m"
#define COLOR_YELLOW "\x1B[0;93m"

static const char *log_colors[]{COLOR_RESET, COLOR_GREEN, COLOR_YELLOW, COLOR_RED};

std::mutex &getMutex();

/**
 * @brief prints to std::cerr
 *
 * @return 1
 */
template <class... Types>
bool inline f_dbLog(std::ostream &out, Types... args) {
	std::lock_guard lock(dbg::getMutex());
	(out << ... << args) << std::endl;
	return 1;
}

/**
 * @def dbLog(severity, ...)
 * If severity is greater than the definition DBG_LOG_LEVEL, prints all arguments to std::cerr
 */

#ifndef NDEBUG
	#define DBG_DEBUG
	#define DBG_LOG_LEVEL -1
	#define dbLog(severity, ...)                                                                                   \
		severity >= DBG_LOG_LEVEL                                                                                  \
			? (dbg::f_dbLog(std::cerr, dbg::log_colors[severity], "[", #severity, "] ", __VA_ARGS__, COLOR_RESET)) \
			: 0;
#else
	#define DBG_LOG_LEVEL		 3
	#define dbLog(severity, ...) ((void)0)
#endif

}	  // namespace dbg
