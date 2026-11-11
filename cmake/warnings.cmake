# Link PRIVATE from every target that compiles this project's sources, so the
# flags never reach dependencies. The presets turn warnings into errors.
add_library(${PROJECT_NAME}_warnings INTERFACE)
add_library(${PROJECT_NAME}::warnings ALIAS ${PROJECT_NAME}_warnings)

target_compile_options(
  ${PROJECT_NAME}_warnings
  INTERFACE -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wconversion
            -Wsign-conversion
            -Wold-style-cast
            -Wnon-virtual-dtor
            -Woverloaded-virtual
            -Wnull-dereference
            -Wdouble-promotion
            -Wfloat-equal
            -Wimplicit-fallthrough
            -Wvla)
