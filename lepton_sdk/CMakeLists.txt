cmake_minimum_required(VERSION 3.15)
project(lepton_sdk)

file(GLOB_RECURSE SOURCES
	"./*.c"
	)

file(GLOB_RECURSE HEADERS
	"./*.h"
	)

add_definitions(-DI2C_DEVICE_FILE="/dev/i2c-2")

# build static library
add_library(lepton_sdk_static STATIC ${SOURCES}
	$<TARGET_OBJECTS:sdk_objects>
	$<TARGET_OBJECTS:net_objects>)
target_include_directories(lepton_sdk_static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${LOG_DIR} ${SDK_DIR})
set_target_properties(lepton_sdk_static PROPERTIES
	OUTPUT_NAME "lepton_sdk"
	)

# build shared library
add_library(lepton_sdk_shared  SHARED ${SOURCES}
	$<TARGET_OBJECTS:sdk_objects>
	$<TARGET_OBJECTS:net_objects>)
target_include_directories(lepton_sdk_shared PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${LOG_DIR} ${SDK_DIR})
set_target_properties(lepton_sdk_shared PROPERTIES
	OUTPUT_NAME "lepton_sdk"
	)

if(WIN32)
	set_target_properties(lepton_sdk_shared PROPERTIES
		ARCHIVE_OUTPUT_NAME "lepton_sdk"
		RUNTIME_OUTPUT_NAME "lepton_sdk"
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
		ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
		)

	set_target_properties(lepton_sdk_static PROPERTIES
		ARCHIVE_OUTPUT_NAME "lepton_sdk"
		ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/static"
		)
endif()

file(GLOB_RECURSE LEPTON_SDK_HEADERS
	"./LEPTON_AGC.h"
	"./LEPTON_CFG.h"
	"./LEPTON_ErrorCodes.h"
	"./LEPTON_Macros.h"
	"./LEPTON_OEM.h"
	"./LEPTON_RAD.h"
	"./LEPTON_SDKConfig.h"
	"./LEPTON_SDK.h"
	"./LEPTON_SYS.h"
	"./LEPTON_Types.h"
	"./LEPTON_VID.h"
	)


install(FILES ${LEPTON_SDK_HEADERS}
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
	)

install(TARGETS lepton_sdk_static lepton_sdk_shared
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
	)
