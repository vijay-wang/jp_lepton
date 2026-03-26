cmake_minimum_required(VERSION 3.15)
project(data_router)

#set(CMAKE_C_STANDARD 11)

file(GLOB CURRENT_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
add_executable(data_router ${CURRENT_SOURCES}
	$<TARGET_OBJECTS:shmq_objects>
	$<TARGET_OBJECTS:sdk_objects>
	$<TARGET_OBJECTS:net_objects>
)

target_link_libraries(data_router PRIVATE pthread)

target_include_directories(data_router PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SHMQ_DIR}
    ${SDK_DIR}
    ${NET_DIR}
)

install(TARGETS data_router
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
