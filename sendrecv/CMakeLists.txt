##
# An example CMakeLists.txt for E2SAR
# ASC 10/24/24
#

project(
  SendRecv VERSION 0.1 DESCRIPTION "Send and receive test" LANGUAGES CXX
  )

add_executable(sendrecv sendrecv.cpp)
target_link_libraries(
  sendrecv
  ${E2SAR_LIBRARIES}
  ${Boost_LIBRARIES}
  ${GRPC_LIBRARIES}
  ${Protobuf_LIBRARIES}
  Threads::Threads
  )

install(TARGETS sendrecv DESTINATION bin)
