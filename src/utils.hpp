#pragma once

#include <atomic>
#include <functional>
#include <string_view>
#include <string>


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

template<class A, class B>
struct std::hash<std::pair<A, B>> {
	std::size_t operator()(const std::pair<A, B> &p) const {
		return std::hash<A>{}(p.first) ^ std::hash<B>{}(p.second);
	}
};

template <>
struct std::hash<std::string> {
  using is_transparent = void;
  [[nodiscard]] size_t operator()(const char *txt) const {
    return std::hash<std::string_view>{}(txt);
  }
  [[nodiscard]] size_t operator()(std::string_view txt) const {
    return std::hash<std::string_view>{}(txt);
  }
  [[nodiscard]] size_t operator()(const std::string &txt) const {
    return std::hash<std::string_view>{}(txt.c_str());
  }
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
