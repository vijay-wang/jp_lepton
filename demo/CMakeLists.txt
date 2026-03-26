cmake_minimum_required(VERSION 3.15)
project(sdk_demo C)

set(CMAKE_C_STANDARD 99)
add_compile_options(-D_POSIX_C_SOURCE=200809L)

# ------------------------------------------------------------------
# Platform
# ------------------------------------------------------------------
if(WIN32)
    set(NET_PLATFORM_SRC ${NET_DIR}/win_net.c)
    set(PLATFORM_LIBS    ws2_32)
    add_compile_definitions(WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
else()
    set(NET_PLATFORM_SRC ${NET_DIR}/unix_net.c)
    set(PLATFORM_LIBS    pthread)
endif()

# ------------------------------------------------------------------
# demo executable
# ------------------------------------------------------------------
add_executable(demo
    demo.c
    ${SDK_DIR}/sdk.c
    ${SDK_DIR}/sdk_frame.c
    ${SDK_DIR}/sdk_image.c
    ${SDK_DIR}/sdk_cmd.c
    ${SDK_DIR}/sdk_file.c
    ${NET_DIR}/net.c
    ${NET_PLATFORM_SRC}
)

target_include_directories(demo PRIVATE ${SDK_DIR} ${NET_DIR})
target_link_libraries(demo ${PLATFORM_LIBS})

add_executable(sdk_demo
    sdk_demo.c
)
target_link_libraries(sdk_demo PRIVATE sdk_shared lepton_sdk_shared pthread m dl)
target_include_directories(sdk_demo PRIVATE ${SDK_DIR} ${LEPTON_SDK_DIR})
