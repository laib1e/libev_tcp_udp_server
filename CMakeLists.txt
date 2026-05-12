cmake_minimum_required(VERSION 3.20)
project(libev_tcp_udp_server CXX C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_path(LIBEV_INCLUDE_DIR ev.h)
find_library(LIBEV_LIBRARY NAMES ev)

if (LIBEV_INCLUDE_DIR AND LIBEV_LIBRARY)
	add_library(ev::ev UNKNOWN IMPORTED)

	set_target_properties(ev::ev PROPERTIES IMPORTED_LOCATION "${LIBEV_LIBRARY}" 
											INTERFACE_INCLUDE_DIRECTORIES "${LIBEV_INCLUDE_DIR}")
else()
	add_library(ev STATIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libev/ev.c)
	set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/third_party/libev/ev.c PROPERTIES LANGUAGE C)
	target_include_directories(ev PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libev)
	target_compile_definitions(ev PRIVATE EV_STANDALONE=1
										  EV_USE_EPOLL=1
										  EV_USE_IOURING=0
    									  _GNU_SOURCE=1)
	add_library(ev::ev ALIAS ev)
endif()

add_executable(libev_tcp_udp_server main.cpp)
target_include_directories(libev_tcp_udp_server PRIVATE include)
target_link_libraries(libev_tcp_udp_server PRIVATE ev::ev)