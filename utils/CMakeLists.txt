set(src FribEjfatUtils.cpp)
set(headers FribEjfatUtils.h)
set(lib FribEjfatUtils)

add_library(${lib} SHARED ${src})

target_include_directories(${lib} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${lib} ${PROJECT_LIBS})

set_target_properties(${lib} PROPERTIES EXPORT_NAME ${lib})

install(TARGETS ${lib} DESTINATION lib)
install(FILES ${headers} DESTINATION include)
