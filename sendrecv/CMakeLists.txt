set(CMAKE_INSTALL_RPATH ${UFMT_LIB} ${NSCLDAQ_LIB} ${CMAKE_INSTALL_PREFIX}/lib)

###########################################################################
# Common includes/libs for SendRecv:
##

set(common_includes
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${UFMT_INC}
  ${NSCLDAQ_INC}
  ${CMAKE_SOURCE_DIR}/utils
  )
set(common_libs
  ${PROJECT_LIBS}
  ${UFMT_LIBS}
  ${NSCLDAQ_LIB}/libException.so
  ${NSCLDAQ_LIB}/liburl.so
  ${NSCLDAQ_LIB}/libDataFlow.so
  )

###########################################################################
# ufmt I/O library:
##

set(src
  DataSource.cpp FdDataSource.cpp StreamDataSource.cpp
  DataSink.cpp FileDataSink.cpp RingDataSink.cpp
  )
set(headers
  DataSource.h FdDataSource.h StreamDataSource.h
  DataSink.h FileDataSink.h RingDataSink.h
  )

# Shared, installed:
set(lib EjfatIO)
add_library(${lib} SHARED ${src})
target_include_directories(${lib} PUBLIC ${common_includes})
target_link_libraries(${lib} ${common_libs})


###########################################################################
# Build and install:
# - Project headers
# - I/O library
# - Executables
##

set(send_exec send_sendrecv)
set(send_src send.cpp)
add_executable(${send_exec} ${send_src})
target_include_directories(${send_exec} PRIVATE ${common_includes})
target_link_libraries(${send_exec} ${common_libs} FribEjfatUtils ${lib})
set_target_properties(${send_exec} PROPERTIES OUTPUT_NAME send)

set(recv_exec recv_sendrecv)
set(recv_src recv.cpp)
add_executable(${recv_exec} ${recv_src})
target_include_directories(${recv_exec} PRIVATE ${common_includes})
target_link_libraries(${recv_exec} ${common_libs} FribEjfatUtils ${lib})
set_target_properties(${recv_exec} PROPERTIES OUTPUT_NAME recv)

install(FILES ${headers} DESTINATION include)
install(TARGETS ${lib} DESTINATION lib)
install(TARGETS ${send_exec} ${recv_exec} DESTINATION bin/sendrecv)
