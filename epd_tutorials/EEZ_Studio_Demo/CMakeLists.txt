cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(epd_hello_world)

target_sources(app PRIVATE src/main.c)

# === UI sources ===
# Use forward slashes with CMake (works on Windows too)
set(UI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/ui)

# Grab everything in src/ui (recursively); CONFIGURE_DEPENDS re-globs on file changes
file(GLOB_RECURSE UI_SOURCES CONFIGURE_DEPENDS
  ${UI_DIR}/*.c
  ${UI_DIR}/*.cpp
)

# Add sources to the app
target_sources(app PRIVATE ${UI_SOURCES})

# If your UI has headers in src/ui or subfolders, add the include path(s)
target_include_directories(app PRIVATE
  ${UI_DIR}
)