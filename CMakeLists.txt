cmake_minimum_required(VERSION 3.20)

project("media-player" CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE SRC CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_executable(${PROJECT_NAME} ${SRC})

set(DEFAULT_COMPILE_FLAGS -Wall -Wextra -Wpedantic -Werror -Wno-c23-extensions)
set(EXTRA_COMPILE_FLAGS "" CACHE STRING "C++ extra compile flags")

target_compile_options(${PROJECT_NAME} PRIVATE ${DEFAULT_COMPILE_FLAGS} "SHELL:${EXTRA_COMPILE_FLAGS}")
target_link_options(${PROJECT_NAME} PRIVATE ${DEFAULT_COMPILE_FLAGS} "SHELL:${EXTRA_COMPILE_FLAGS}")
target_include_directories(${PROJECT_NAME} PRIVATE include)

# Link ffmpeg
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

# Link glfw and audio
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(RTAUDIO_INSTALL OFF CACHE BOOL "" FORCE)
set(RTAUDIO_BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)

include(FetchContent)

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

# Link Vulkan and compile shaders
find_package(Vulkan REQUIRED COMPONENTS glslc)

set(SHADER_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SHADER_BIN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders/dist)

file(GLOB SHADER_SOURCES CONFIGURE_DEPENDS
    ${SHADER_SRC_DIR}/*.vert
    ${SHADER_SRC_DIR}/*.frag
    ${SHADER_SRC_DIR}/*.comp
)

set(SPIRV_OUTPUTS)
foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(NAME ${SHADER} NAME)
    set(OUT ${SHADER_BIN_DIR}/${NAME}.spv)
    add_custom_command(
        OUTPUT ${OUT}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_BIN_DIR}
        COMMAND Vulkan::glslc ${SHADER} -o ${OUT}
        DEPENDS ${SHADER}
        COMMENT "Compiling ${NAME}"
        VERBATIM
    )
    list(APPEND SPIRV_OUTPUTS ${OUT})
endforeach()

add_custom_target(shaders ALL DEPENDS ${SPIRV_OUTPUTS})

add_dependencies(${PROJECT_NAME} shaders)
target_sources(${PROJECT_NAME} PRIVATE ${SPIRV_OUTPUTS})
target_link_libraries(${PROJECT_NAME} PRIVATE Vulkan::Vulkan)

# Link-time optimization
option(LTO "Enable Link-time Optimization" ON)
if(LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_output)
    if(ipo_supported)
        set_target_properties(${PROJECT_NAME} PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
endif()
