cmake_minimum_required(VERSION 3.15...3.30)

project(
	NetworkHomework
	VERSION 1.0
	LANGUAGES CXX)

# Prepare doctest for other targets to use

SET(CMAKE_CXX_COMPILER clang++)
SET(CMAKE_CXX_FLAGS "-Wall -Wextra -Wpedantic -fsanitize=address -g")

file(GLOB_RECURSE FIGURES_SOURCES
	./src/*.cpp
)

find_package(Threads REQUIRED)

# Make server application
add_executable(server main.cpp ${FIGURES_SOURCES})
set_target_properties(server PROPERTIES RUNTIME_OUTPUT_DIRECTORY ../)
target_compile_features(server PRIVATE cxx_std_20)
target_include_directories(server PUBLIC ./lib/)
target_include_directories(server PUBLIC ./src/)

target_link_libraries(server PRIVATE Threads::Threads)
