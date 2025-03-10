project(FribEjfatUtils VERSION 0.1
  DESCRIPTION "Utilities for FRIB-E2SAR processing"
  LANGUAGES CXX
  )

set(src FribEjfatUtils.cpp)
set(headers FribEjfatUtils.h)
set(lib FribEjfatUtils)

add_library(${lib} STATIC ${src})

target_include_directories(${lib} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${lib} ${PROJECT_LIBS})

set_target_properties(${lib} PROPERTIES EXPORT_NAME ${lib})

## TODO (ASC 3/3/25): Make and install shared library:
#install(TARGETS ${lib} DESTINATION lib)
install(FILES ${headers} DESTINATION include)
