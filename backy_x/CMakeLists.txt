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

# SftpTarget library
add_library(SftpTarget_lib STATIC
    src/targets/SftpTarget.cpp
)
target_link_libraries(SftpTarget_lib 
    PUBLIC backy_x_core       # For IStorageTarget
    PUBLIC CredentialManager_lib # Changed from PRIVATE to PUBLIC
)
target_include_directories(SftpTarget_lib PUBLIC include)

# Link SftpTarget_lib with CURL and LibSSH2
# CURL::libcurl and LibSSH2::libssh2 are modern CMake targets if find_package works well.
# The existing CMakeLists.txt already calls find_package for CURL and LibSSH2.
if(CURL_FOUND)
    target_link_libraries(SftpTarget_lib PRIVATE CURL::libcurl)
else()
    message(WARNING "CURL not found. SftpTarget_lib will not have CURL functionality.")
endif()

if(LibSSH2_FOUND)
    target_link_libraries(SftpTarget_lib PRIVATE LibSSH2::libssh2)
else()
    message(WARNING "LibSSH2 not found. SftpTarget_lib may have limited SFTP functionality if CURL relies on it and it's missing.")
endif()


# CredentialManager library
set(CREDMAN_SOURCES src/util/CredentialManager.cpp) # Common base .cpp

if(APPLE)
    list(APPEND CREDMAN_SOURCES src/util/CredentialManager_mac.mm)
elseif(CMAKE_SYSTEM_NAME MATCHES "Linux" AND NOT ANDROID)
    list(APPEND CREDMAN_SOURCES src/util/CredentialManager_linux.cpp)
elseif(WIN32)
    list(APPEND CREDMAN_SOURCES src/util/CredentialManager_win.cpp)
endif()

add_library(CredentialManager_lib STATIC ${CREDMAN_SOURCES})
target_include_directories(CredentialManager_lib PUBLIC include)
target_link_libraries(CredentialManager_lib PUBLIC Qt6::Core) # For QString etc. in the interface

# Now, add platform-specific libraries to CredentialManager_lib
if(APPLE)
    # For Keychain services. Security.framework is the one.
    target_link_libraries(CredentialManager_lib PRIVATE Security) 
elseif(CMAKE_SYSTEM_NAME MATCHES "Linux" AND NOT ANDROID)
    if(LIBSECRET_FOUND) # Check if pkg_check_modules found it
        target_link_libraries(CredentialManager_lib PRIVATE ${LIBSECRET_LIBRARIES})
        target_include_directories(CredentialManager_lib PRIVATE ${LIBSECRET_INCLUDE_DIRS}) # If it has specific includes
    else()
        message(WARNING "Libsecret not found. CredentialManager on Linux will be a stub.")
    endif()
elseif(WIN32)
    # For CredEnumerate, CredRead, CredWrite, CredDelete functions
    # They are in Advapi32.dll, and linked via Advapi32.lib
    target_link_libraries(CredentialManager_lib PRIVATE Advapi32.lib)
endif()

# --- Executables ---

# CLI application
add_executable(backy_x_cli src/cli/main_cli.cpp)
target_link_libraries(backy_x_cli PRIVATE LocalTarget_lib)
# Add other necessary libraries like std::filesystem if needed explicitly on some compilers/platforms
# For M0, let's assume it links fine with CMAKE_CXX_STANDARD 17.
# Note: SftpTarget_lib is not directly added to backy_x_cli or backy_full_gui yet.
# This will be done when it's ready to be used.

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
    SftpTarget_lib      
    CredentialManager_lib 
)
target_include_directories(backy_full_gui PRIVATE ${CMAKE_SOURCE_DIR}/src)

if(APPLE)
    target_link_libraries(backy_full_gui PRIVATE Security)
endif()

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

# SftpTarget Tests
add_executable(test_sftp_target tests/test_sftp_target.cpp)
target_link_libraries(test_sftp_target PRIVATE
    SftpTarget_lib  # The library we are testing
    gtest_main      # For GoogleTest
    # CURL::libcurl and LibSSH2::libssh2 are linked by SftpTarget_lib
    # Qt6::Core might be needed if SftpTarget or CredentialManager uses Qt types directly in headers
    # SftpTarget uses CredentialManager, which uses Qt6::Core. SftpTarget_lib links backy_x_core, which links Qt6::Core.
    # CredentialManager_lib is also linked to SftpTarget (implicitly via SftpTarget's own .cpp including CredentialManager.h & create function)
    # or should be explicitly if SftpTarget_lib doesn't build CredentialManager itself.
    # Let's assume SftpTarget_lib correctly pulls in what it needs from CredentialManager.
    # If linking fails for Qt types, add Qt6::Core here.
)
if(APPLE)
    target_link_libraries(test_sftp_target PRIVATE Security)
endif()
# Add test to CTest
add_test(NAME SftpTargetTests COMMAND test_sftp_target)


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
