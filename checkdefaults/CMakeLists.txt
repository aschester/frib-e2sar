project(CheckDefaults VERSION 0.1
  DESCRIPTION "Check default E2SAR settings"
  LANGUAGES CXX
  )

set(exec checkdefaults)
set(src checkdefaults.cpp)

add_executable(${exec} ${src})
target_link_libraries(${exec} ${PROJECT_LIBS})

install(TARGETS ${exec} DESTINATION bin/checkdefaults)
