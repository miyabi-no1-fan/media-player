cmake_minimum_required(VERSION 3.20)

project("media-player" CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(LTO "Enable Link-time Optimization" ON)

file(GLOB_RECURSE SRC CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_executable(${PROJECT_NAME} ${SRC})

target_include_directories(${PROJECT_NAME} PRIVATE include)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET
    libavfilter
    libavformat
    libavcodec
    libswresample
    libavutil
)
target_link_libraries(${PROJECT_NAME} PRIVATE PkgConfig::LIBAV)

find_package(RtAudio QUIET)
if(RtAudio_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE RtAudio::rtaudio)
else()
    pkg_check_modules(RtAudio REQUIRED IMPORTED_TARGET rtaudio)
    target_link_libraries(${PROJECT_NAME} PRIVATE PkgConfig::RtAudio)
endif()

find_package(Vulkan REQUIRED)
find_package(glfw3 CONFIG REQUIRED)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Vulkan::Vulkan
    glfw
)

if(LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_output)
    if(ipo_supported)
        set_target_properties(${PROJECT_NAME} PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
endif()
