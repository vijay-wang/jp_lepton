cmake_minimum_required(VERSION 3.15)
project(lepton_data_collector)

#set(CMAKE_C_STANDARD 11)

file(GLOB CURRENT_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
add_executable(lepton_data_collector ${CURRENT_SOURCES} $<TARGET_OBJECTS:shmq_objects>)
target_include_directories(lepton_data_collector PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SHMQ_DIR}
)
