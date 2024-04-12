cmake_minimum_required(VERSION 3.23)

project(
  DgCat
  VERSION 0.0.0
  DESCRIPTION "A tool to copy high-rate, unidirectional datagram streams between UDP, files, pipes, etc., while preserving message boundaries."
  HOMEPAGE_URL https://github.com/mckelvie-org/dg-cat
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

#set(Boost_VERBOSE                ON)
#set(Boost_DEBUG                  ON)

set(Boost_USE_STATIC_LIBS        ON)
set(Boost_USE_DEBUG_LIBS         ON)
set(Boost_USE_RELEASE_LIBS       ON)
set(Boost_USE_MULTITHREADED      ON)
set(Boost_USE_STATIC_RUNTIME    OFF)
find_package(argparse REQUIRED)
find_package(Boost 1.84.0 REQUIRED COMPONENTS log log_setup)
if(Boost_FOUND)
  include_directories(${Boost_INCLUDE_DIRS})
endif()

add_library(dg_cat src/dg_cat.cpp)
target_sources(
  dg_cat PUBLIC
  FILE_SET HEADERS
  BASE_DIRS include
  FILES include/dg_cat/dg_cat.hpp
)
target_compile_features(dg_cat PRIVATE cxx_std_11)

add_executable(dg_cat_exe src/main.cpp)
set_property(TARGET dg_cat_exe PROPERTY OUTPUT_NAME dg-cat)
target_link_libraries(dg_cat_exe PUBLIC ${Boost_LIBRARIES} argparse::argparse dg_cat )

# Declare test targets if it is not a subproject and testing is enabled.
if(NOT SUBPROJECT AND BUILD_TESTING)
  enable_testing()

  find_package(Catch2 REQUIRED)

  get_target_property(dg_cat_SOURCES dg_cat SOURCES)
  get_target_property(dg_cat_HEADER_DIRS dg_cat HEADER_DIRS)

  add_executable(dg_cat_test test/dg_cat_test.cpp ${dg_cat_SOURCES})
  set_property(TARGET dg_cat_test PROPERTY OUTPUT_NAME dg-cat-test)
  target_include_directories(dg_cat_test PRIVATE ${dg_cat_HEADER_DIRS})
  target_link_libraries(dg_cat_test PRIVATE ${Boost_LIBRARIES} Catch2::Catch2WithMain)

  include(CheckCoverage)
  target_check_coverage(dg_cat_test)

  include(Catch)
  catch_discover_tests(dg_cat_test)
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
    TARGETS dg_cat_exe dg_cat
    EXPORT dg_cat_targets
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
    FILE_SET HEADERS
  )

  install(
    EXPORT dg_cat_targets
    FILE DgCatTargets.cmake
    NAMESPACE dg_cat::
    DESTINATION lib/cmake/DgCat
  )

  include(CMakePackageConfigHelpers)
  write_basic_package_version_file(
    DgCatConfigVersion.cmake
    COMPATIBILITY SameMajorVersion
  )

  #install(
  #  FILES
  #    cmake/DgCatConfig.cmake
  #    ${CMAKE_CURRENT_BINARY_DIR}/DgCatConfigVersion.cmake
  #  DESTINATION lib/cmake/DgCat
  #)

endif()
