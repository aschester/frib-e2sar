find_package(Doxygen REQUIRED dot)

set(DOXYGEN_GENERATE_HTML YES)
set(DOXYGEN_HTML_OUTPUT ${CMAKE_BINARY_DIR}/docs/html)
set(DOXYGEN_USE_MDFILE_AS_MAINPAGE "${CMAKE_SOURCE_DIR}/README.md")
set(DOXYGEN_EXCLUDE_PATTERNS
  *.py
  ${CMAKE_SOURCE_DIR}/UnifiedFormat/*
  ${CMAKE_BINARY_DIR}/UnifiedFormat/*
  ${CMAKE_BINARY_DIR}/fmtconfig.h
  ${CMAKE_BINARY_DIR}/utils/tsvalidate/cmdline.h
  )

doxygen_add_docs(doxygen_target
  ${CMAKE_SOURCE_DIR}
  ALL
  COMMENT "Generate man pages"
  )

install(DIRECTORY ${CMAKE_BINARY_DIR}/docs/html DESTINATION share/html
  PATTERN "*.*~" EXCLUDE
  PATTERN "#*" EXCLUDE)
