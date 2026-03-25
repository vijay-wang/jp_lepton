cmake_minimum_required(VERSION 3.15)
project(lepton_controller)

#set(CMAKE_C_STANDARD 11)

set(SHMQ_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../shmq)

file(GLOB SHMQ_SOURCES "${SHMQ_DIR}/*.c")
add_library(shmq_objects OBJECT ${SHMQ_SOURCES})
target_include_directories(shmq_objects PRIVATE ${SHMQ_DIR})

file(GLOB CURRENT_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
add_executable(lepton_controller ${CURRENT_SOURCES} $<TARGET_OBJECTS:shmq_objects>)
target_include_directories(lepton_controller PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SHMQ_DIR}
)
