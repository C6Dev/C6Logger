cmake_minimum_required(VERSION 3.15)
project(C6Logger)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCES src/Logger.cpp)
set(HEADERS include/Logger.h)

# Build the static library
add_library(C6LoggerLib STATIC ${SOURCES} ${HEADERS})

set_target_properties(C6LoggerLib PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)