cmake_minimum_required(VERSION 3.20.0)

# Must be before find_package(Zephyr ...)
set(ZEPHYR_EXTRA_MODULES "${CMAKE_CURRENT_SOURCE_DIR}/modules/button")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(button_driver_example)

target_sources(app PRIVATE src/main.c)
