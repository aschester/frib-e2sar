cmake_minimum_required(VERSION 3.18)
project(E2SAR_Examples VERSION 0.1 DESCRIPTION "E2SAR Examples" LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

find_package(
  Boost COMPONENTS system url thread chrono random program_options REQUIRED
  )
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
find_program(PKG_CONFIG_EXECUTABLE pkg-config)
pkg_check_modules(E2SAR REQUIRED e2sar)
pkg_check_modules(GRPC REQUIRED grpc++)
pkg_check_modules(Protobuf REQUIRED protobuf)

set(NSCLDAQ_ROOT "_"  CACHE STRING "Path to NSCLDAQ installation")
set(NSCLDAQ_INC "${NSCLDAQ_ROOT}/include" CACHE STRING "NSCLDAQ include dir")
set(NSCLDAQ_LIB "${NSCLDAQ_ROOT}/lib" CACHE STRING "NSCLDAQ lib dir")
message(STATUS "Using NSLCDAQ: ${NSCLDAQ_ROOT}")

# For now no ringbuffer support from ufmt:

set(UFMT_ROOT "${NSCLDAQ_ROOT}/unifiedformat" CACHE STRING "Path to unified format library installation")
set(UFMT_INC "${UFMT_ROOT}/include" CACHE STRING "Unified format include dir")
set(UFMT_LIB "${UFMT_ROOT}/lib" CACHE STRING "Unified format lib dir")
message(STATUS "Using NSCLDAQ unified format: ${UFMT_ROOT}")

add_compile_options(-g -O2 -Wall)

add_subdirectory(utils)
add_subdirectory(io)
add_subdirectory(checkdefaults)
add_subdirectory(sendrecv)

set(ini_files reassembler_config.ini segmenter_config.ini)
install(FILES ${ini_files} DESTINATION ini)

