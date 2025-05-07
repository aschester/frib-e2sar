add_custom_target(
  tsvalidatecmdopts ALL
  COMMAND gengetopt <${CMAKE_CURRENT_SOURCE_DIR}/tsvalidateargs.ggo
  COMMAND $(CC) -c cmdline.c
  SOURCES tsvalidateargs.ggo
  COMMENT "Building gengetopt args parser"
  BYPRODUCTS   cmdline.o cmdline.h
  )

add_executable(
  tsvalidate
  tsvalidate.cpp cmdline.o cmdline.h
  ${CMAKE_SOURCE_DIR}/e2sarwf/DataSource.cpp
  ${CMAKE_SOURCE_DIR}/e2sarwf/FdDataSource.cpp
  ${CMAKE_SOURCE_DIR}/e2sarwf/StreamDataSource.cpp
  ${CMAKE_SOURCE_DIR}/e2sarwf/RingDataSource.cpp
  )

target_compile_options(tsvalidate PUBLIC -g -O2)
add_dependencies(tsvalidate tsvalidatecmdopts)

target_link_libraries(tsvalidate
  ${UFMT_LIBS}
  ${NSCLDAQ_LIB}/libdaqshm.so
  ${NSCLDAQ_LIB}/libDataFlow.so
  ${NSCLDAQ_LIB}/libException.so
  ${NSCLDAQ_LIB}/liburl.so
  )
target_include_directories(tsvalidate PRIVATE 
  ${CMAKE_CURRENT_SOURCE_DIR}/../../
  ${CMAKE_CURRENT_SOURCE_DIR}/../../abstract
  ${CMAKE_SOURCE_DIR}/e2sarwf
  ${UFMT_INC}
  ${NSCLDAQ_INC}
  ${CMAKE_BINARY_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}
  )

target_link_options(tsvalidate PUBLIC -g -Wl,-rpath=${CMAKE_INSTALL_PREFIX}/lib -Wl,-rpath=${NSCLDAQ_LIB})

install(TARGETS tsvalidate DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)
