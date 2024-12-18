cmake_minimum_required(VERSION 3.15...3.30)

project(
	NetworkHomework
	VERSION 1.0
	LANGUAGES CXX)

# Prepare doctest for other targets to use

SET(CMAKE_CXX_COMPILER clang++)
SET(CMAKE_CXX_FLAGS "-std=c++20 -Wall -Wextra -Wpedantic -g")

file(GLOB_RECURSE PROJECT_SOURCES
	./src/*.cpp
)

find_package(Threads REQUIRED)

# Make server application
add_executable(server main.cpp ${PROJECT_SOURCES})
set_target_properties(server PROPERTIES RUNTIME_OUTPUT_DIRECTORY ../)
target_compile_definitions(server PUBLIC DBG_LOG_LEVEL=0)
target_include_directories(server PUBLIC ./lib/)
target_include_directories(server PUBLIC ./src/)

target_link_libraries(server PRIVATE Threads::Threads)

add_executable(client client.cpp ${PROJECT_SOURCES})
set_target_properties(client PROPERTIES RUNTIME_OUTPUT_DIRECTORY ../)
target_include_directories(client PUBLIC ./lib/)
target_include_directories(client PUBLIC ./src/)
target_link_libraries(client PRIVATE Threads::Threads)





