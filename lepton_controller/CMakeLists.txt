cmake_minimum_required(VERSION 3.15)
project(lepton_controller)

#set(CMAKE_C_STANDARD 11)

file(GLOB CURRENT_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
add_executable(lepton_controller ${CURRENT_SOURCES} $<TARGET_OBJECTS:shmq_objects>)
target_include_directories(lepton_controller PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${SHMQ_DIR}
	${LOG_DIR}
	)

install(TARGETS lepton_controller
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
	)

install(FILES S20lepton_controller
	DESTINATION ${CMAKE_INSTALL_INITDIR}
	)
