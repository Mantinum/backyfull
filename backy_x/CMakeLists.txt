cmake_minimum_required(VERSION 3.22)
project(BackyFull VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) # Disable compiler-specific extensions for better portability

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent Core Gui Test)
# Gui is often a transitive dependency of Widgets but explicitly listing it is safer.

# --- External Libraries ---
# Find CURL
find_package(CURL REQUIRED)
# Usage: Link against CURL::libcurl if available (CMake >= 3.12), 
# otherwise use CURL_LIBRARIES and CURL_INCLUDE_DIRS.

# Find LibSSH2
find_package(LibSSH2)
if(NOT LibSSH2_FOUND)
    message(WARNING "LibSSH2 not found. SFTP features will be disabled. "
                    "Please install libssh2 development package (e.g., libssh2-1-dev on Debian/Ubuntu, "
                    "libssh2-devel on Fedora, brew install libssh2 on macOS) "
                    "and ensure it's in CMake's search path (or set LibSSH2_DIR).")
endif()
# Usage: Link against LibSSH2::libssh2 if available,
# otherwise use LIBSSH2_LIBRARIES and LIBSSH2_INCLUDE_DIRS.

# Find Libsecret (Linux only)
if(CMAKE_SYSTEM_NAME MATCHES "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBSECRET REQUIRED libsecret-1)
    # Usage: Link against LIBSECRET_LIBRARIES and include LIBSECRET_INCLUDE_DIRS.
    # PkgConfig typically sets these variables.
endif()

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

# Core library
add_library(backy_x_core STATIC 
    src/core/Scheduler.cpp
    include/core/Scheduler.h 
    # Add other future core .cpp files here, e.g. BackupEngine.cpp
)
target_include_directories(backy_x_core PUBLIC include) # PUBLIC so consumers get include paths
target_link_libraries(backy_x_core PUBLIC Qt6::Core) # PUBLIC if consumers need Qt types from core headers, PRIVATE otherwise.
                                                     # Let's use PUBLIC for now as Scheduler signals might be used by GUI.

# LocalTarget library
add_library(LocalTarget_lib STATIC 
    src/targets/LocalTarget.cpp
    # include/targets/LocalTarget.h # Headers are usually not listed here if "include" is an include_directory
)
target_link_libraries(LocalTarget_lib PUBLIC backy_x_core) # LocalTarget might use IStorageTarget from core
target_include_directories(LocalTarget_lib PUBLIC include) # So users of LocalTarget_lib find its headers and IStorageTarget

# CredentialManager library
set(CREDMAN_SOURCES src/util/CredentialManager.cpp) # Common base .cpp

if(APPLE)
    list(APPEND CREDMAN_SOURCES src/util/CredentialManager_mac.mm)
elif(CMAKE_SYSTEM_NAME MATCHES "Linux" AND NOT ANDROID)
    list(APPEND CREDMAN_SOURCES src/util/CredentialManager_linux.cpp)
elif(WIN32)
    list(APPEND CREDMAN_SOURCES src/util/CredentialManager_win.cpp)
endif()

add_library(CredentialManager_lib STATIC ${CREDMAN_SOURCES})
target_include_directories(CredentialManager_lib PUBLIC include)
target_link_libraries(CredentialManager_lib PUBLIC Qt6::Core) # For QString etc. in the interface

# Now, add platform-specific libraries to CredentialManager_lib
if(APPLE)
    # For Keychain services. Security.framework is the one.
    target_link_libraries(CredentialManager_lib PRIVATE Security) 
elif(CMAKE_SYSTEM_NAME MATCHES "Linux" AND NOT ANDROID)
    if(LIBSECRET_FOUND) # Check if pkg_check_modules found it
        target_link_libraries(CredentialManager_lib PRIVATE ${LIBSECRET_LIBRARIES})
        target_include_directories(CredentialManager_lib PRIVATE ${LIBSECRET_INCLUDE_DIRS}) # If it has specific includes
    else()
        message(WARNING "Libsecret not found. CredentialManager on Linux will be a stub.")
    endif()
elif(WIN32)
    # For CredEnumerate, CredRead, CredWrite, CredDelete functions
    # They are in Advapi32.dll, and linked via Advapi32.lib
    target_link_libraries(CredentialManager_lib PRIVATE Advapi32.lib)
endif()

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

# GUI Application
add_executable(backy_full_gui
    src/gui/main_gui.cpp
    src/gui/MainWindow.cpp
)
target_link_libraries(backy_full_gui PRIVATE
    Qt6::Widgets 
    Qt6::Concurrent 
    backy_x_core    
    LocalTarget_lib 
)
target_include_directories(backy_full_gui PRIVATE ${CMAKE_SOURCE_DIR}/src)

# For Windows, to make it a GUI application without a console window
if(WIN32)
    set_target_properties(backy_full_gui PROPERTIES WIN32_EXECUTABLE ON)
endif()


# --- Tests ---
# Enable testing for the project
enable_testing()

add_executable(test_local_target tests/test_local_target.cpp)
target_link_libraries(test_local_target PRIVATE LocalTarget_lib gtest_main) # gtest_main includes GTest::gtest and GTest::gmock

# Add test to CTest
# Note: GoogleTest discovers tests automatically if linked with gtest_main
add_test(NAME LocalTargetTests COMMAND test_local_target)

# Scheduler Tests
add_executable(test_scheduler tests/test_scheduler.cpp)
target_link_libraries(test_scheduler PRIVATE
    backy_x_core  # Contains Scheduler, links Qt6::Core
    gtest_main    # For GoogleTest
    Qt6::Test   # QSignalSpy is in Core, but if using QTest macros, add this
)
# Add test to CTest
add_test(NAME SchedulerTests COMMAND test_scheduler)


# --- Installation (Placeholder for M0) ---
# This will be expanded for CPack later
install(TARGETS backy_x_cli backy_full_gui DESTINATION bin) 
# install(FILES include/core/IStorageTarget.h DESTINATION include/core)
# install(FILES include/targets/LocalTarget.h DESTINATION include/targets)


# --- Build Types ---
# Basic build type configurations. Users can override with -DCMAKE_BUILD_TYPE
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to 'Release' as none was specified.")
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()
