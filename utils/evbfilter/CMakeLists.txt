set(CMAKE_INSTALL_RPATH ${NSCLDAQ_LIB} ${CMAKE_INSTALL_PREFIX}/lib)

set(includes
  ${NSCLDAQ_INC}
  ${UFMT_INC}
  )

# Ensure we have the correct NSCLDAQ libs:
find_library(daqshm NAMES libdaqshm.so PATHS ${NSCLDAQ_LIB})
if(NOT daqshm)
  message(FATAL_ERROR "Cannot find libdaqshm.so in ${NSCLDAQ_LIB}")
endif()
find_library(dataformat NAMES libdataformat.so PATHS ${NSCLDAQ_LIB})
if(NOT dataformat)
  message(FATAL_ERROR "Cannot find libdataformat.so in ${NSCLDAQ_LIB}")
endif()

set(libs
  ${Boost_LIBRARIES}
  ${daqshm}
  ${dataformat}
  ${UFMT_LIBS}
  )

set(exec evbfilter)
set(src evbfilter.cpp)
add_executable(${exec} ${src})
target_include_directories(${exec} PRIVATE ${includes})
target_link_libraries(${exec} ${libs})
install(TARGETS ${exec} DESTINATION bin)
