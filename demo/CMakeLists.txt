cmake_minimum_required(VERSION 3.15)
project(sdk_demo C)

add_executable(sdk_demo
	sdk_demo.c
	)

if(UNIX)
        target_link_libraries(sdk_demo PRIVATE sdk_shared lepton_sdk_shared m dl)
else()
        target_link_libraries(sdk_demo PRIVATE sdk_static lepton_sdk_static)
endif()
target_include_directories(sdk_demo PRIVATE ${SDK_DIR} ${LEPTON_SDK_DIR} ${LOG_DIR})

install(TARGETS sdk_demo
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
	)
