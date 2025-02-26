project(
  SendRecv VERSION 0.1 DESCRIPTION "Use E2SAR to transmit NSLCDAQ data"
  LANGUAGES CXX
  )

set(CMAKE_INSTALL_RPATH ${UFMT_LIB} ${NSCLDAQ_LIB} ${CMAKE_INSTALL_PREFIX}/lib)

add_executable(send send.cpp)
target_include_directories(
  send PRIVATE
  ${CMAKE_SOURCE_DIR}/io
  ${CMAKE_SOURCE_DIR}/utils
  ${UFMT_INC}
  ${NSCLDAQ_INC}
  )
target_link_libraries(
  send
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

add_executable(recv recv.cpp)
target_include_directories(
  recv PRIVATE
  ${CMAKE_SOURCE_DIR}/io
  ${CMAKE_SOURCE_DIR}/utils
  ${UFMT_INC}
  ${NSCLDAQ_INC}
  )
target_link_libraries(
  recv
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

install(TARGETS send recv DESTINATION bin/sendrecv)
