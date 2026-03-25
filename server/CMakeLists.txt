cmake_minimum_required(VERSION 3.15)
project(data_router)

#set(CMAKE_C_STANDARD 11)

set(SHMQ_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../shmq)
set(SDK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../sdk)
set(NET_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../net)

file(GLOB SHMQ_SOURCES "${SHMQ_DIR}/*.c")
add_library(shmq_objects OBJECT ${SHMQ_SOURCES})
target_include_directories(shmq_objects PRIVATE ${SHMQ_DIR})

file(GLOB SDK_SOURCES "${SDK_DIR}/*.c")
add_library(sdk_objects OBJECT ${SDK_SOURCES})
target_include_directories(sdk_objects PRIVATE ${SDK_DIR})

add_subdirectory(${NET_DIR} net)

file(GLOB CURRENT_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
add_executable(data_router ${CURRENT_SOURCES}
	$<TARGET_OBJECTS:shmq_objects>
	$<TARGET_OBJECTS:sdk_objects>
)

target_link_libraries(data_router PRIVATE net_shared)

target_include_directories(data_router PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SHMQ_DIR}
    ${SDK_DIR}
    ${NET_DIR}
)
