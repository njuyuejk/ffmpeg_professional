cmake_minimum_required(VERSION 3.28)
project(ffmpeg_professional_git)

set(CMAKE_CXX_STANDARD 17)

# 设置 FFmpeg 的根目录
set(FFMPEG_ROOT "D:/project/C++/my/ffmpeg-multi/ffmpeg4")

# 添加包含目录
include_directories(include)
include_directories("${FFMPEG_ROOT}/include")

# 设置库目录
link_directories("${FFMPEG_ROOT}/libwin64")

# 要链接的 FFmpeg 库
set(FFMPEG_LIBS
        avcodec
        avformat
        avutil
        swscale
        swresample
)

# 查找 FFmpeg 库
foreach(lib ${FFMPEG_LIBS})
    find_library(${lib}_LIBRARY NAMES ${lib} PATHS "${FFMPEG_ROOT}/libwin64")
    if(NOT ${lib}_LIBRARY)
        message(FATAL_ERROR "FFmpeg library ${lib} not found in ${FFMPEG_ROOT}/libwin64")
    endif()
    list(APPEND FOUND_FFMPEG_LIBS ${${lib}_LIBRARY})
endforeach()

set(app
        src/app/application.cpp
        include/app/application.h
)

set(config
        src/config/config.cpp
        include/config/config.h
)

set(common
        src/common/threadpool.cpp
        include/common/threadpool.h
        src/common/utils.cpp
        include/common/utils.h
        src/common/common.cpp
        include/common/common.h

)

set(ffmpeg_base
        src/ffmpeg_base/decoder.cpp
        include/ffmpeg_base/decoder.h
        src/ffmpeg_base/encoder.cpp
        include/ffmpeg_base/encoder.h
        src/ffmpeg_base/hw_accel.cpp
        include/ffmpeg_base/hw_accel.h
        src/ffmpeg_base/stream_processor.cpp
        include/ffmpeg_base/stream_processor.h
        src/ffmpeg_base/stream_manager.cpp
        include/ffmpeg_base/stream_manager.h

)

set(logger
        include/logger/logger.h
        src/logger/logger.cpp
)

add_executable(ffmpeg_professional_git main.cpp ${app} ${common} ${ffmpeg_base} ${logger} ${config})

# 链接 FFmpeg 库
target_link_libraries(ffmpeg_professional_git PRIVATE ${FOUND_FFMPEG_LIBS} pthread)
