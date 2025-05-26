cmake_minimum_required(VERSION 3.22)
project(BackyX VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) # Disable compiler-specific extensions for better portability

# --- Global Settings ---
# Define an include directory for cleaner #include paths
include_directories(include)

# --- GoogleTest ---
# Add GoogleTest - typically via FetchContent or find_package
# For M0, we'll use FetchContent for simplicity in CI
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip # Example: Use a specific release
  # Or use GIT_REPOSITORY and GIT_TAG for more flexibility
  # GIT_REPOSITORY https://github.com/google/googletest.git
  # GIT_TAG        release-1.12.1 
)
# For Windows: Prevent overriding the parent project's compiler options.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE) 
FetchContent_MakeAvailable(googletest)


# --- Project Libraries ---

# Core library (placeholder for now, will grow)
add_library(backy_x_core INTERFACE) # Interface library for headers only, or add sources later
target_include_directories(backy_x_core INTERFACE include)
# Add source files to backy_x_core if/when they are created in src/core

# LocalTarget library
add_library(LocalTarget_lib STATIC 
    src/targets/LocalTarget.cpp
    # include/targets/LocalTarget.h # Headers are usually not listed here if "include" is an include_directory
)
target_link_libraries(LocalTarget_lib PUBLIC backy_x_core) # LocalTarget might use IStorageTarget from core
target_include_directories(LocalTarget_lib PUBLIC include) # So users of LocalTarget_lib find its headers and IStorageTarget

# --- Executables ---

# CLI application
add_executable(backy_x_cli src/cli/main_cli.cpp)
target_link_libraries(backy_x_cli PRIVATE LocalTarget_lib)
# Add other necessary libraries like std::filesystem if needed explicitly on some compilers/platforms
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # For GCC and Clang, std::filesystem might require linking -lstdc++fs or similar if not C++17 default
    # However, with C++17 set, it should generally link automatically.
    # If link errors occur, specific linker flags might be needed here.
    # For M0, let's assume it links fine with CMAKE_CXX_STANDARD 17.
elseif(MSVC)
    # MSVC usually links std::filesystem automatically with /std:c++17
endif()


# --- Tests ---
# Enable testing for the project
enable_testing()

add_executable(test_local_target tests/test_local_target.cpp)
target_link_libraries(test_local_target PRIVATE LocalTarget_lib gtest_main) # gtest_main includes GTest::gtest and GTest::gmock

# Add test to CTest
# Note: GoogleTest discovers tests automatically if linked with gtest_main
add_test(NAME LocalTargetTests COMMAND test_local_target)


# --- Installation (Placeholder for M0) ---
# This will be expanded for CPack later
install(TARGETS backy_x_cli DESTINATION bin) 
# install(FILES include/core/IStorageTarget.h DESTINATION include/core)
# install(FILES include/targets/LocalTarget.h DESTINATION include/targets)


# --- Build Types ---
# Basic build type configurations. Users can override with -DCMAKE_BUILD_TYPE
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to 'Release' as none was specified.")
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()
