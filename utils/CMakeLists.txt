project(
  FribEjfatUtils VERSION 0.1 DESCRIPTION "Utilities for FRIB-E2SAR development"
  LANGUAGES CXX
  )

set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH ${UFMT_LIB})

set(src FribEjfatUtils.cpp)
set(headers FribEjfatUtils.h)
set(lib FribEjfatUtils)

add_library(${lib} STATIC ${src})

target_include_directories(${lib} PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${UFMT_INC}
)
target_link_libraries(
  ${lib}
  ${E2SAR_LIBRARIES}
  ${Boost_LIBRARIES}
  ${GRPC_LIBRARIES}
  ${Protobuf_LIBRARIES}
  Threads::Threads
  )

set_target_properties(${lib} PROPERTIES EXPORT_NAME ${lib})

install(TARGETS ${lib} DESTINATION lib)
install(FILES ${headers} DESTINATION include)
