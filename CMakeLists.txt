cmake_minimum_required(VERSION 3.15)
project(device_sdk_server C)

set(SHMQ_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shmq)
set(NET_DIR ${CMAKE_CURRENT_SOURCE_DIR}/net)
set(SDK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/sdk)

file(GLOB SHMQ_SOURCES "${SHMQ_DIR}/*.c")
add_library(shmq_objects OBJECT ${SHMQ_SOURCES})
target_include_directories(shmq_objects PRIVATE ${SHMQ_DIR})

file(GLOB NET_SOURCES "${NET_DIR}/*.c")
add_library(net_objects OBJECT ${NET_SOURCES})
set_target_properties(net_objects PROPERTIES
	POSITION_INDEPENDENT_CODE ON
	)
target_include_directories(net_objects PRIVATE ${NET_DIR})

file(GLOB SDK_SOURCES "${SDK_DIR}/*.c")
add_library(sdk_objects OBJECT ${SDK_SOURCES})
target_include_directories(sdk_objects PRIVATE ${SDK_DIR})

add_subdirectory(./demo)

add_subdirectory(./lepton_sdk)

add_subdirectory(./lepton_data_collector)

add_subdirectory(./lepton_controller)

add_subdirectory(./server)

add_subdirectory(./sdk)
