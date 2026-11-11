# The system GoogleTest when present, else the pinned release (the pin matches
# Arch's version; update URL and hash together).
# -DFETCHCONTENT_TRY_FIND_PACKAGE_MODE=NEVER forces the download.
include(FetchContent)

FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.18.0.tar.gz
  URL_HASH
    SHA256=6e3191c1455468b3fc35a417fb565c1c5071aee1b7e7f85e30cf48a98d37d8b5
  SYSTEM FIND_PACKAGE_ARGS NAMES GTest)
block()
set(CMAKE_COMPILE_WARNING_AS_ERROR OFF)
set(CMAKE_CXX_CLANG_TIDY "")
set(INSTALL_GTEST OFF)
FetchContent_MakeAvailable(googletest)
endblock()

include(GoogleTest)
