# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/Users/chiradip/codes/DB25-parser2/build/_deps/db25_tokenizer-src")
  file(MAKE_DIRECTORY "/Users/chiradip/codes/DB25-parser2/build/_deps/db25_tokenizer-src")
endif()
file(MAKE_DIRECTORY
  "/Users/chiradip/codes/DB25-parser2/build/_deps/db25_tokenizer-build"
  "/Users/chiradip/codes/DB25-parser2/build/_deps/db25_tokenizer-subbuild/db25_tokenizer-populate-prefix"
  "/Users/chiradip/codes/DB25-parser2/build/_deps/db25_tokenizer-subbuild/db25_tokenizer-populate-prefix/tmp"
  "/Users/chiradip/codes/DB25-parser2/build/_deps/db25_tokenizer-subbuild/db25_tokenizer-populate-prefix/src/db25_tokenizer-populate-stamp"
  "/Users/chiradip/codes/DB25-parser2/build/_deps/db25_tokenizer-subbuild/db25_tokenizer-populate-prefix/src"
  "/Users/chiradip/codes/DB25-parser2/build/_deps/db25_tokenizer-subbuild/db25_tokenizer-populate-prefix/src/db25_tokenizer-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/Users/chiradip/codes/DB25-parser2/build/_deps/db25_tokenizer-subbuild/db25_tokenizer-populate-prefix/src/db25_tokenizer-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/Users/chiradip/codes/DB25-parser2/build/_deps/db25_tokenizer-subbuild/db25_tokenizer-populate-prefix/src/db25_tokenizer-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
