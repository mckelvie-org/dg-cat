cmake_minimum_required(VERSION 3.23)

project(
  UdpCapture
  VERSION 0.0.0
  DESCRIPTION "A tool to capture incoming high-rate, unidirectional UDP streams to a file or pipe, preserving message boundaries."
  HOMEPAGE_URL https://github.com/mckelvie-org/udp-capture
  LANGUAGES CXX
)

# Check if this project is a subproject of another project.
if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  set(SUBPROJECT TRUE)
endif()

# Append the module path and export to the parent scope if is a subproject.
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
if(SUBPROJECT)
  set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} PARENT_SCOPE)
endif()

# Enable warning checks if it is not a subproject and testing is enabled.
if(NOT SUBPROJECT AND BUILD_TESTING)
  find_package(CheckWarning REQUIRED)
  include(CheckWarning)
  add_check_warning()
endif()

find_package(argparse REQUIRED)

add_library(udp_capture src/udp_capture.cpp)
target_sources(
  udp_capture PUBLIC
  FILE_SET HEADERS
  BASE_DIRS include
  FILES include/udp_capture/udp_capture.hpp
)
target_compile_features(udp_capture PRIVATE cxx_std_11)

add_executable(udp_capture_exe src/main.cpp)
set_property(TARGET udp_capture_exe PROPERTY OUTPUT_NAME udp-capture)
target_link_libraries(udp_capture_exe PUBLIC argparse::argparse udp_capture)

# Declare test targets if it is not a subproject and testing is enabled.
if(NOT SUBPROJECT AND BUILD_TESTING)
  enable_testing()

  find_package(Catch2 REQUIRED)

  get_target_property(udp_capture_SOURCES udp_capture SOURCES)
  get_target_property(udp_capture_HEADER_DIRS udp_capture HEADER_DIRS)

  add_executable(udp_capture_test test/udp_capture_test.cpp ${udp_capture_SOURCES})
  set_property(TARGET udp_capture_test PROPERTY OUTPUT_NAME udp-capture-test)
  target_include_directories(udp_capture_test PRIVATE ${udp_capture_HEADER_DIRS})
  target_link_libraries(udp_capture_test PRIVATE Catch2::Catch2WithMain)

  include(CheckCoverage)
  target_check_coverage(udp_capture_test)

  include(Catch)
  catch_discover_tests(udp_capture_test)
endif()

# Enable automatic formatting if it is not a subproject and testing is enabled.
if(NOT SUBPROJECT AND BUILD_TESTING)
  find_package(FixFormat REQUIRED)
  include(FixFormat)
  add_fix_format()
endif()

# Declare export and install targets if it is not a subproject.
if(NOT SUBPROJECT)
  install(
    TARGETS udp_capture_exe udp_capture
    EXPORT udp_capture_targets
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
    FILE_SET HEADERS
  )

  install(
    EXPORT udp_capture_targets
    FILE UdpCaptureTargets.cmake
    NAMESPACE udp_capture::
    DESTINATION lib/cmake/UdpCapture
  )

  include(CMakePackageConfigHelpers)
  write_basic_package_version_file(
    UdpCaptureConfigVersion.cmake
    COMPATIBILITY SameMajorVersion
  )

  install(
    FILES
      cmake/UdpCaptureConfig.cmake
      ${CMAKE_CURRENT_BINARY_DIR}/UdpCaptureConfigVersion.cmake
    DESTINATION lib/cmake/UdpCapture
  )
endif()
