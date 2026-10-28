cmake_minimum_required(VERSION 3.20)

project("media-player" CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(LTO "Enable Link-time Optimization" ON)
set(FLAGS "" CACHE STRING "C++ compile flags")

file(GLOB_RECURSE SRC CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_executable(${PROJECT_NAME} ${SRC})

target_compile_options(${PROJECT_NAME} PRIVATE "SHELL:${FLAGS}")
target_link_options(${PROJECT_NAME} PRIVATE "SHELL:${FLAGS}")
target_include_directories(${PROJECT_NAME} PRIVATE include)

if(WIN32)
    set(FFMPEG_ROOT "$ENV{FFMPEG_DIR}")
    target_include_directories(${PROJECT_NAME} PRIVATE "${FFMPEG_ROOT}/include")
    target_link_libraries(${PROJECT_NAME} PRIVATE
        "${FFMPEG_ROOT}/lib/avcodec.lib"
        "${FFMPEG_ROOT}/lib/avformat.lib"
        "${FFMPEG_ROOT}/lib/avutil.lib"
        "${FFMPEG_ROOT}/lib/avfilter.lib"
        "${FFMPEG_ROOT}/lib/swresample.lib"
    )
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET
        libavfilter
        libavformat
        libavcodec
        libswresample
        libavutil
    )
    target_link_libraries(${PROJECT_NAME} PRIVATE PkgConfig::LIBAV)
endif()

find_package(Vulkan REQUIRED) # require Vulkan SDK 1.4.350 or newer
target_link_libraries(${PROJECT_NAME} PRIVATE Vulkan::Vulkan)

include(FetchContent)

set(FETCHCONTENT_QUIET FALSE CACHE BOOL "Show FetchContent logs" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(RTAUDIO_INSTALL OFF CACHE BOOL "" FORCE)
set(RTAUDIO_BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)

FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.5.1
    GIT_PROGRESS TRUE
)
FetchContent_Declare(rtaudio
    GIT_REPOSITORY https://github.com/thestk/rtaudio.git
    GIT_TAG 6.0.1
    GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(glfw rtaudio)
target_link_libraries(${PROJECT_NAME} PRIVATE glfw rtaudio)

if(LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_output)
    if(ipo_supported)
        set_target_properties(${PROJECT_NAME} PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
endif()
