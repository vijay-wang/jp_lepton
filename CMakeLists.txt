cmake_minimum_required(VERSION 3.15)
project(device_sdk_server C)

set(CMAKE_C_STANDARD 99)
add_compile_options(-D_POSIX_C_SOURCE=200809L)

# ------------------------------------------------------------------
# Platform detection
# ------------------------------------------------------------------
if(WIN32)
    set(NET_PLATFORM_SRC net/win_net.c)
    set(PLATFORM_LIBS    ws2_32)
    add_compile_definitions(WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
else()
    set(NET_PLATFORM_SRC net/unix_net.c)
    set(PLATFORM_LIBS    pthread)
endif()

# ------------------------------------------------------------------
# net  – static library (shared by sdk and server)
# ------------------------------------------------------------------
add_library(net STATIC
    net/net.c
    ${NET_PLATFORM_SRC}
)
target_include_directories(net PUBLIC net)
target_link_libraries(net ${PLATFORM_LIBS})

# ------------------------------------------------------------------
# sdk  – static library (cross-platform, links net)
# ------------------------------------------------------------------
add_library(sdk STATIC
    sdk/sdk_frame.c
    sdk/sdk_image.c
    sdk/sdk_cmd.c
    sdk/sdk_file.c
    sdk/sdk.c
)
target_include_directories(sdk PUBLIC sdk net)
target_link_libraries(sdk net)

# ------------------------------------------------------------------
# server  – executable (Linux only, links sdk + net)
# ------------------------------------------------------------------
if(NOT WIN32)
    add_executable(server
        server/main.c
        server/server.c
        server/server_session.c
        server/shmq.c
    )
    target_include_directories(server PRIVATE sdk net server)
    target_link_libraries(server sdk net)
endif()

add_subdirectory(./demo)
