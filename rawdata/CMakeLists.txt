project(
  RawData VERSION 0.1 DESCRIPTION "Use E2SAR to transmit raw NSLCDAQ data"
  LANGUAGES CXX
  )

set(CMAKE_INSTALL_RPATH ${UFMT_LIB} ${NSCLDAQ_LIB} ${CMAKE_INSTALL_PREFIX}/lib)

set(send_exec send_rawdata)
set(send_src send.cpp)

add_executable(${send_exec} ${send_src})
target_include_directories(
  ${send_exec} PRIVATE
  ${CMAKE_SOURCE_DIR}/io
  ${CMAKE_SOURCE_DIR}/utils
  ${UFMT_INC}
  ${NSCLDAQ_INC}
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
  ${NSCLDAQ_LIB}/libException.so
  ${NSCLDAQ_LIB}/liburl.so
  EjfatIO
  FribEjfatUtils
  )
set_target_properties(${send_exec} PROPERTIES OUTPUT_NAME send)

set(recv_exec recv_rawdata)
set(recv_src recv.cpp)

add_executable(${recv_exec} ${recv_src})
target_include_directories(
  ${recv_exec} PRIVATE
  ${CMAKE_SOURCE_DIR}/io
  ${CMAKE_SOURCE_DIR}/utils
  ${UFMT_INC}
  ${NSCLDAQ_INC}
  )
target_link_libraries(
  ${recv_exec}
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
  ${NSCLDAQ_LIB}/libException.so
  EjfatIO
  FribEjfatUtils
  )
set_target_properties(${recv_exec} PROPERTIES OUTPUT_NAME recv)

install(TARGETS ${send_exec} ${recv_exec} DESTINATION bin/rawdata)
