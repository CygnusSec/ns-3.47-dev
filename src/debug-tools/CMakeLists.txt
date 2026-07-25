build_lib(
  LIBNAME debug-tools
  SOURCE_FILES helper/simulation-debug-helper.cc
  HEADER_FILES helper/simulation-debug-helper.h
  LIBRARIES_TO_LINK ${libinternet}
)
