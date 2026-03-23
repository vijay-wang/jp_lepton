cmake_minimum_required(VERSION 3.15)
project(sdk_demo C)

set(CMAKE_C_STANDARD 99)
add_compile_options(-D_POSIX_C_SOURCE=200809L)

# ------------------------------------------------------------------
# Paths (demo lives at project/demo/, sources are in ../sdk ../net)
# ------------------------------------------------------------------
set(SDK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../sdk)
set(NET_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../net)

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
