cmake_minimum_required(VERSION 3.21)
project(audio_async_loopback C)
set(CMAKE_C_STANDARD 11)

include_directories(${GLFW_INCLUDE_DIRS})
file(GLOB SOURCE
        ${CMAKE_SOURCE_DIR}/*.c
        ${CMAKE_SOURCE_DIR}/*.h)
add_executable(${PROJECT_NAME}
        ${SOURCE})
target_link_libraries(${PROJECT_NAME}
        /usr/lib/x86_64-linux-gnu/libpulse.so
        /usr/lib/x86_64-linux-gnu/libpulse-simple.so
        /usr/lib/x86_64-linux-gnu/libsamplerate.so
        /usr/lib/x86_64-linux-gnu/libpthread.so.0
        /usr/lib/x86_64-linux-gnu/libavutil.so
        /usr/lib/x86_64-linux-gnu/libavcodec.so)
