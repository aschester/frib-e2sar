# Ensure we have the correct NSCLDAQ libs:
find_library(Exception NAMES libException.so PATHS ${NSCLDAQ_LIB})
if(NOT Exception)
  message(FATAL_ERROR "Cannot find libException.so in ${NSCLDAQ_LIB}")
endif()
find_library(DataFlow NAMES libDataFlow.so PATHS ${NSCLDAQ_LIB})
if(NOT DataFlow)
  message(FATAL_ERROR "Cannot find libDataFlow.so in ${NSCLDAQ_LIB}")
endif()
find_library(daqshm NAMES libdaqshm.so PATHS ${NSCLDAQ_LIB})
if(NOT daqshm)
  message(FATAL_ERROR "Cannot find libdaqshm.so in ${NSCLDAQ_LIB}")
endif()
find_library(url NAMES liburl.so PATHS ${NSCLDAQ_LIB})
if(NOT url)
  message(FATAL_ERROR "Cannot find liburl.so in ${NSCLDAQ_LIB}")
endif()


add_custom_target(
  tsvalidatecmdopts ALL
  COMMAND gengetopt <${CMAKE_CURRENT_SOURCE_DIR}/tsvalidateargs.ggo
  COMMAND $(CC) -c cmdline.c
  SOURCES tsvalidateargs.ggo
  COMMENT "Building gengetopt args parser"
  BYPRODUCTS cmdline.o cmdline.h
  )

add_executable(
  tsvalidate
  tsvalidate.cpp cmdline.o cmdline.h
  ${CMAKE_SOURCE_DIR}/e2sarwf/DataSource.cpp
  ${CMAKE_SOURCE_DIR}/e2sarwf/FdDataSource.cpp
  ${CMAKE_SOURCE_DIR}/e2sarwf/StreamDataSource.cpp
  ${CMAKE_SOURCE_DIR}/e2sarwf/RingDataSource.cpp
  )

target_compile_options(tsvalidate PUBLIC -g -O2 -Wall)
add_dependencies(tsvalidate tsvalidatecmdopts)

target_include_directories(tsvalidate PRIVATE 
  ${CMAKE_CURRENT_SOURCE_DIR}/../../
  ${CMAKE_CURRENT_SOURCE_DIR}/../../abstract
  ${CMAKE_SOURCE_DIR}/e2sarwf
  ${UFMT_INC}
  ${NSCLDAQ_INC}
  ${CMAKE_BINARY_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}
  )
target_link_libraries(tsvalidate
  ${UFMT_LIBS}
  ${daqshm}
  ${DataFlow}
  ${Exception}
  ${url}
  )
target_link_options(tsvalidate PUBLIC -g -Wl,-rpath=${CMAKE_INSTALL_PREFIX}/lib -Wl,-rpath=${NSCLDAQ_LIB})

install(TARGETS tsvalidate DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)
