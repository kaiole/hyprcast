# Everything in this directory is linked into the plugin, a shared object.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)

# libjson has no releases and no package to find; the pin is a commit archive.
# Update URL and hash together, and the same pin in PKGBUILD.
FetchContent_Declare(
  libjson
  URL https://github.com/kaiole/libjson/archive/013397b90735d630d89eab79c2d097bf2ec9876d.tar.gz
  URL_HASH
    SHA256=f2c86456ba8d0ea0b5ad94ae2c562f63532116455a208d5616149ad27be3ea36
  SYSTEM)
block()
set(CMAKE_COMPILE_WARNING_AS_ERROR OFF)
set(CMAKE_CXX_CLANG_TIDY "")
# libjson registers its own tests with ctest when BUILD_TESTING is on.
set(BUILD_TESTING OFF)
FetchContent_MakeAvailable(libjson)
endblock()

find_package(PkgConfig REQUIRED)

if(PROJECT_IS_TOP_LEVEL AND BUILD_TESTING)
  add_subdirectory(test_support)
endif()

add_subdirectory(log)
add_subdirectory(config)
add_subdirectory(wire)
add_subdirectory(server)
add_subdirectory(session)
add_subdirectory(adapter)
