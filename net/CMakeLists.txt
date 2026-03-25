cmake_minimum_required(VERSION 3.15)
project(net)

file(GLOB_RECURSE SOURCES
    "./*.c"
)

file(GLOB_RECURSE HEADERS
    "./*.h"
)

add_library(net_static STATIC ${SOURCES})
target_include_directories(net_static PUBLIC ./)
set_target_properties(net_static PROPERTIES
	OUTPUT_NAME "net"
	POSITION_INDEPENDENT_CODE ON
)

add_library(net_shared  SHARED ${SOURCES})
target_include_directories(net_shared PUBLIC ./)
set_target_properties(net_shared PROPERTIES
	OUTPUT_NAME "net"
)

if(WIN32)
	set_target_properties(mylib_shared PROPERTIES
		ARCHIVE_OUTPUT_NAME "net.dll"
		RUNTIME_OUTPUT_NAME "net"
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
		ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
		)

	set_target_properties(mylib_static PROPERTIES
		ARCHIVE_OUTPUT_NAME "net"
		ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/static"
		)
endif()
