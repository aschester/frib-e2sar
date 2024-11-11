##
# E2SAR send-receive programs
# ASC 10/24/24
#

project(
  SimData VERSION 0.1 DESCRIPTION "Use E2SAR to send NSLCDAQ data"
  LANGUAGES CXX
  )

set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH ${UFMT_LIB} ${NSCLDAQ_LIB} ${DDASFMT_LIB})

set(send_exec send_simdata)
set(send_src send.cpp DataSource.cpp FdDataSource.cpp StreamDataSource.cpp)
set(send_headers DataSource.h FdDataSource.h StreamDataSource.h)

add_executable(${send_exec} ${send_src})
target_include_directories(
  ${send_exec} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${UFMT_INC}
  ${NSCLDAQ_INC}
  ${DDASFMT_INC}
  )
target_link_libraries(
  ${send_exec}
  ${E2SAR_LIBRARIES}
  ${Boost_LIBRARIES}
  ${GRPC_LIBRARIES}
  ${Protobuf_LIBRARIES}
  Threads::Threads
  ${UFMT_LIB}/libNSCLDAQFormat.so
  ${UFMT_LIB}/libV10Format.so
  ${UFMT_LIB}/libV11Format.so
  ${UFMT_LIB}/libV12Format.so
  ${UFMT_LIB}/libAbstractFormat.so
  ${NSCLDAQ_LIB}/libDDASDataSimulator.so
  ${NSCLDAQ_LIB}/libException.so
  ${NSCLDAQ_LIB}/liburl.so
  ${DDASFMT_LIB}/libDDASFormat.so
  )

set(recv_exec recv_simdata)
set(recv_src recv.cpp)
add_executable(${recv_exec} ${recv_src})
target_link_libraries(
  ${recv_exec}
  ${E2SAR_LIBRARIES}
  ${Boost_LIBRARIES}
  ${GRPC_LIBRARIES}
  ${Protobuf_LIBRARIES}
  Threads::Threads
  )

install(FILES ${send_headers} DESTINATION include)
install(TARGETS ${send_exec} ${recv_exec} DESTINATION bin)
