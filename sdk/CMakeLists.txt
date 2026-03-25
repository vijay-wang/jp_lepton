cmake_minimum_required(VERSION 3.15)
project(sdk)

#set(CMAKE_C_STANDARD 11)

set(SHMQ_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../shmq)
set(NET_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../net)
set(SDK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/)

file(GLOB SHMQ_SOURCES "${SHMQ_DIR}/*.c")
add_library(shmq_objects OBJECT ${SHMQ_SOURCES})
target_include_directories(shmq_objects PRIVATE ${SHMQ_DIR})

add_subdirectory(${NET_DIR} net)

file(GLOB CURRENT_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.c")

# build static library
add_library(sdk_static STATIC ${CURRENT_SOURCES})
target_include_directories(sdk_static PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${SHMQ_DIR}
	${SDK_DIR}
	${NET_DIR}
	)

target_link_libraries(sdk_static PRIVATE net_static)
set_target_properties(sdk_static PROPERTIES
	OUTPUT_NAME "sdk"
	)

# build share library
add_library(sdk_shared  SHARED ${CURRENT_SOURCES})
target_include_directories(sdk_shared PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${SHMQ_DIR}
	${SDK_DIR}
	${NET_DIR}
	)
target_link_libraries(sdk_shared PRIVATE net_static)
set_target_properties(sdk_shared PROPERTIES
	OUTPUT_NAME "sdk"
	)

if(WIN32)
	set_target_properties(sdk_shared PROPERTIES
		ARCHIVE_OUTPUT_NAME "sdk.dll"
		RUNTIME_OUTPUT_NAME "sdk"
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
		ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
		)

	set_target_properties(sdk_static PROPERTIES
		ARCHIVE_OUTPUT_NAME "sdk"
		ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/static"
		)
endif()

