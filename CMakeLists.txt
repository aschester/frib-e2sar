###########################################################################
# Build requirements:
##

cmake_minimum_required(VERSION 3.18)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

###########################################################################
# Project info:
##

project(FRIB_E2SAR VERSION 0.1.0
  DESCRIPTION "FRIB-E2SAR data processing"
  LANGUAGES CXX
  )

###########################################################################
# Additional requirements:
##

find_package(Boost COMPONENTS system url thread chrono random program_options
  REQUIRED
  )
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
find_program(PKG_CONFIG_EXECUTABLE pkg-config)
pkg_check_modules(E2SAR REQUIRED e2sar)
pkg_check_modules(GRPC REQUIRED grpc++)
pkg_check_modules(Protobuf REQUIRED protobuf)

set(PROJECT_LIBS
  ${E2SAR_LIBRARIES}
  ${Boost_LIBRARIES}
  ${GRPC_LIBRARIES}
  ${Protobuf_LIBRARIES}
  Threads::Threads
  CACHE STRING "Common project libs required by E2SAR"
  )

###########################################################################
# Our env:
##

set(NSCLDAQ_ROOT "_"  CACHE STRING "Path to NSCLDAQ installation")
set(NSCLDAQ_INC "${NSCLDAQ_ROOT}/include" CACHE STRING "NSCLDAQ include dir")
set(NSCLDAQ_LIB "${NSCLDAQ_ROOT}/lib" CACHE STRING "NSCLDAQ lib dir")
message(STATUS "Using NSLCDAQ: ${NSCLDAQ_ROOT}")

set(UFMT_INC
  ${CMAKE_BINARY_DIR}
  ${CMAKE_SOURCE_DIR}/UnifiedFormat
  ${CMAKE_SOURCE_DIR}/UnifiedFormat/abstract
  CACHE STRING "Unified format include dirs"
  )
set(UFMT_LIBS
  ${CMAKE_BINARY_DIR}/UnifiedFormat/libNSCLDAQFormat.so
  ${CMAKE_BINARY_DIR}/UnifiedFormat/v10/libV10Format.so
  ${CMAKE_BINARY_DIR}/UnifiedFormat/v11/libV11Format.so
  ${CMAKE_BINARY_DIR}/UnifiedFormat/v12/libV12Format.so
  ${CMAKE_BINARY_DIR}/UnifiedFormat/abstract/libAbstractFormat.so
  CACHE STRING "Unified format libs"
  )

###########################################################################
# Build and install:
##

# -Wall shows a lot of warnings from e.g. NSCLDAQ which we suppress.
add_compile_options(-g -O2 #-Wall
  )

# Installation for subdirs handled in their own CMakeList.txt:
add_subdirectory(UnifiedFormat)
add_subdirectory(checkdefaults)
add_subdirectory(e2sarwf)
add_subdirectory(docs)

# Install ini and script directories as-is:
install(DIRECTORY ini/ DESTINATION ini
  PATTERN "*~" EXCLUDE
  PATTERN "#*" EXCLUDE)
install(DIRECTORY scripts/ DESTINATION scripts FILE_PERMISSIONS
  OWNER_READ OWNER_WRITE OWNER_EXECUTE
  GROUP_READ GROUP_WRITE GROUP_EXECUTE
  WORLD_READ WORLD_EXECUTE
  PATTERN "*~" EXCLUDE
  PATTERN "#*" EXCLUDE)
