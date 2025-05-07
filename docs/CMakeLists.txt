find_package(Doxygen REQUIRED dot)

set(DOXYGEN_GENERATE_HTML YES)
set(DOXYGEN_HTML_OUTPUT ${PROJECT_BINARY_DIR}/docs/html)
set(DOXYGEN_USE_MDFILE_AS_MAINPAGE "${CMAKE_SOURCE_DIR}/README.md")
set(DOXYGEN_EXCLUDE_PATTERNS */UnifiedFormat/*)

doxygen_add_docs(doxygen_target
  ${CMAKE_SOURCE_DIR}
  ALL
  COMMENT "Generate man pages"
  )

install(DIRECTORY ${PROJECT_BINARY_DIR}/docs/html DESTINATION share/html
  PATTERN "*.*~" EXCLUDE
  PATTERN "#*" EXCLUDE)
