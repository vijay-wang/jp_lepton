cmake_minimum_required(VERSION 3.15)
project(sdk)

#set(CMAKE_C_STANDARD 11)

file(GLOB CURRENT_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.c")

# build static library
add_library(sdk_static STATIC ${CURRENT_SOURCES} $<TARGET_OBJECTS:net_objects>)

target_include_directories(sdk_static PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${SHMQ_DIR}
	${SDK_DIR}
	${NET_DIR}
	)

set_target_properties(sdk_static PROPERTIES
	OUTPUT_NAME "sdk"
	)

# build share library
add_library(sdk_shared  SHARED ${CURRENT_SOURCES} $<TARGET_OBJECTS:net_objects>)

target_include_directories(sdk_shared PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${SHMQ_DIR}
	${SDK_DIR}
	${NET_DIR}
	)
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
