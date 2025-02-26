project(
  FribEjfatUtils VERSION 0.1 DESCRIPTION "Utilities for FRIB-E2SAR development"
  LANGUAGES CXX
  )

set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH ${UFMT_LIB})

add_library(FribEjfatUtils SHARED FribEjfatUtils.cpp)

target_include_directories(FribEjfatUtils PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${UFMT_INC}
)
target_link_libraries(
  FribEjfatUtils
  ${E2SAR_LIBRARIES}
  ${Boost_LIBRARIES}
  ${GRPC_LIBRARIES}
  ${Protobuf_LIBRARIES}
  Threads::Threads
  )

set_target_properties(FribEjfatUtils PROPERTIES EXPORT_NAME FribEjfatUtils)

install(TARGETS FribEjfatUtils DESTINATION lib)
install(FILES FribEjfatUtils.h DESTINATION include)
