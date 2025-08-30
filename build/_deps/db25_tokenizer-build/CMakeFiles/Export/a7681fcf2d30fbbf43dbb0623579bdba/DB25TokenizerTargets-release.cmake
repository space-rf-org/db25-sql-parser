#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "DB25::db25_tokenizer" for configuration "Release"
set_property(TARGET DB25::db25_tokenizer APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(DB25::db25_tokenizer PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libdb25tokenizer.a"
  )

list(APPEND _cmake_import_check_targets DB25::db25_tokenizer )
list(APPEND _cmake_import_check_files_for_DB25::db25_tokenizer "${_IMPORT_PREFIX}/lib/libdb25tokenizer.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
