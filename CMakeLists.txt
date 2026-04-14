cmake_minimum_required(VERSION 3.15)
project(device_sdk_server C)

message(STATUS "Using C compiler: ${CMAKE_C_COMPILER}")
message(STATUS "Using CXX compiler: ${CMAKE_CXX_COMPILER}")

set(SHMQ_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shmq)
set(NET_DIR ${CMAKE_CURRENT_SOURCE_DIR}/net)
set(SDK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/sdk)
set(LOG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/log)
set(LEPTON_SDK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lepton_sdk)
set(CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}" CACHE PATH "Install path")
set(CMAKE_INSTALL_BINDIR "bin")
set(CMAKE_INSTALL_LIBDIR "lib")
set(CMAKE_INSTALL_INCLUDEDIR "include")
set(CMAKE_INSTALL_INITDIR "etc/init.d")

file(GLOB NET_SOURCES "${NET_DIR}/*.c")
add_library(net_objects OBJECT ${NET_SOURCES})
set_target_properties(net_objects PROPERTIES
	POSITION_INDEPENDENT_CODE ON
	)
target_include_directories(net_objects PRIVATE ${NET_DIR} ${LOG_DIR})

file(GLOB SDK_SOURCES "${SDK_DIR}/*.c")
add_library(sdk_objects OBJECT ${SDK_SOURCES})
set_target_properties(sdk_objects PROPERTIES
	POSITION_INDEPENDENT_CODE ON
	)
target_include_directories(sdk_objects PRIVATE ${SDK_DIR} ${LOG_DIR})

if(UNIX)
	file(GLOB SHMQ_SOURCES "${SHMQ_DIR}/*.c")
	add_library(shmq_objects OBJECT ${SHMQ_SOURCES})
	target_include_directories(shmq_objects PRIVATE ${SHMQ_DIR} ${LOG_DIR})

	if(BUILD_LEPTON_DATA_COLLECTOR OR BUILD_ALL)
		add_subdirectory(./lepton_data_collector)
	endif()

	if(BUILD_LEPTON_CONTROLLER OR BUILD_ALL)
		add_subdirectory(./lepton_controller)
	endif()

	if(BUILD_SERVER OR BUILD_ALL)
		add_subdirectory(./server)
	endif()
endif()

if(BUILD_SDK OR BUILD_ALL)
	add_subdirectory(./sdk)
endif()

if(BUILD_LEPTON_SDK OR BUILD_ALL)
	add_subdirectory(./lepton_sdk)
endif()

if(BUILD_DEMO OR BUILD_ALL)
	add_subdirectory(./demo)
endif()
