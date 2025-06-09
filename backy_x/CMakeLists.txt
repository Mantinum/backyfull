cmake_minimum_required(VERSION 3.22)
project(BackyFull VERSION 0.1.0 LANGUAGES CXX)

# --------------------------------------------------------------------
# ============== options générales ===================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS  OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# --------------------------------------------------------------------
# ============== dépendances =========================================
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Concurrent Core Gui Test Network NetworkAuth)
find_package(CURL    REQUIRED)
find_package(LibSSH2)          # optionnel : SFTP

if(UNIX AND NOT APPLE AND NOT ANDROID)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBSECRET REQUIRED libsecret-1)
endif()

# -------- GoogleTest : on télécharge exactement la version voulue ---
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)  # on n’installe pas la lib avec notre projet
FetchContent_MakeAvailable(googletest)      # crée les cibles GTest::gtest[ _main ]

# --------------------------------------------------------------------
# ============== chemins project/include =============================
set(PROJECT_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# --------------------------------------------------------------------
# ============== bibliothèques internes ==============================
add_library(backy_x_core STATIC
    src/core/Scheduler.cpp
    ${PROJECT_INCLUDE}/core/Scheduler.h
)
target_include_directories(backy_x_core PUBLIC ${PROJECT_INCLUDE})
target_link_libraries(backy_x_core PUBLIC Qt6::Core)
target_compile_options(backy_x_core PRIVATE -Wall -Wextra -pedantic)

# -------- CredentialManager -----------------------------------------
set(CREDMAN_SRC src/util/CredentialManager.cpp)
if(APPLE)
    list(APPEND CREDMAN_SRC src/util/CredentialManager_mac.mm)
elseif(UNIX AND NOT ANDROID)
    list(APPEND CREDMAN_SRC src/util/CredentialManager_linux.cpp)
else()
    list(APPEND CREDMAN_SRC src/util/CredentialManager_win.cpp)
endif()

add_library(CredentialManager_lib STATIC ${CREDMAN_SRC})
target_include_directories(CredentialManager_lib PUBLIC ${PROJECT_INCLUDE})
target_link_libraries(CredentialManager_lib PUBLIC Qt6::Core)
target_compile_options(CredentialManager_lib PRIVATE -Wall -Wextra -pedantic)
if(APPLE)
    target_link_libraries(CredentialManager_lib PRIVATE "-framework Security" "-framework CoreFoundation")
elseif(UNIX AND NOT ANDROID)
    target_link_libraries(CredentialManager_lib PRIVATE ${LIBSECRET_LIBRARIES})
    target_include_directories(CredentialManager_lib PRIVATE ${LIBSECRET_INCLUDE_DIRS})
else()
    target_link_libraries(CredentialManager_lib PRIVATE Advapi32.lib)
endif()

# -------- Targets de sauvegarde -------------------------------------
add_library(LocalTarget_lib STATIC src/targets/LocalTarget.cpp)
target_include_directories(LocalTarget_lib PUBLIC ${PROJECT_INCLUDE})
target_link_libraries(LocalTarget_lib PUBLIC backy_x_core)
target_compile_options(LocalTarget_lib PRIVATE -Wall -Wextra -pedantic)

add_library(SftpTarget_lib STATIC src/targets/SftpTarget.cpp)
target_include_directories(SftpTarget_lib PUBLIC ${PROJECT_INCLUDE})
target_link_libraries(SftpTarget_lib PUBLIC backy_x_core CredentialManager_lib Qt6::Network)
target_compile_options(SftpTarget_lib PRIVATE -Wall -Wextra -pedantic)
if(CURL_FOUND)
    target_link_libraries(SftpTarget_lib PRIVATE CURL::libcurl)
endif()
if(LibSSH2_FOUND)
    target_link_libraries(SftpTarget_lib PRIVATE LibSSH2::libssh2)
endif()

add_library(GcsTarget_lib STATIC src/targets/GcsTarget.cpp)
target_include_directories(GcsTarget_lib PUBLIC ${PROJECT_INCLUDE})
target_link_libraries(GcsTarget_lib PUBLIC
    backy_x_core
    CredentialManager_lib
    Qt6::Core
    Qt6::Network
    Qt6::Gui      # For QDesktopServices, QUrl, QJsonDocument etc.
    CURL::libcurl
)
target_compile_options(GcsTarget_lib PRIVATE -Wall -Wextra -pedantic)
# Note: Qt${QT_VERSION_MAJOR} will be resolved to Qt6 by existing find_package(Qt6 ...)

# --------------------------------------------------------------------
# ============== exécutables =========================================
add_executable(backy_x_cli src/cli/main_cli.cpp)
target_link_libraries(backy_x_cli PRIVATE LocalTarget_lib CURL::libcurl)
target_compile_options(backy_x_cli PRIVATE -Wall -Wextra -pedantic)

add_executable(backy_full_gui
    src/gui/main_gui.cpp
    src/gui/MainWindow.cpp
    src/gui/DestinationStack.cpp
)
target_sources(backy_full_gui PRIVATE src/cloud/gcs/GcsAuth.cpp)
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/cloud/gcs/GcsAuth.cpp")
    message(FATAL_ERROR "GcsAuth.cpp missing from target!")
endif()
target_include_directories(backy_full_gui PRIVATE src)
target_link_libraries(backy_full_gui
    PRIVATE Qt6::Widgets Qt6::Concurrent Qt6::Network Qt6::NetworkAuth
            LocalTarget_lib SftpTarget_lib GcsTarget_lib CredentialManager_lib)
target_compile_options(backy_full_gui PRIVATE -Wall -Wextra -pedantic)
if(WIN32)
    set_target_properties(backy_full_gui PROPERTIES WIN32_EXECUTABLE ON)
endif()

# --------------------------------------------------------------------
# ============== tests unitaire GoogleTest ===========================
enable_testing()
include(GoogleTest)  # fournit la macro gtest_discover_tests

function(add_backy_test NAME SRC)
    add_executable(${NAME} ${SRC})
    target_link_libraries(${NAME} PRIVATE
        GTest::gtest_main      # une seule cible, plus de doublon
        ${ARGN}                # libs du projet
    )
    target_compile_options(${NAME} PRIVATE -Wall -Wextra -pedantic)
    gtest_discover_tests(${NAME})
endfunction()

add_backy_test(test_local_target   tests/test_local_target.cpp   LocalTarget_lib)
add_backy_test(test_scheduler      tests/test_scheduler.cpp      backy_x_core Qt6::Test)
add_backy_test(test_sftp_target    tests/test_sftp_target.cpp    SftpTarget_lib)

# --------------------------------------------------------------------
# ============== installation ========================================
install(TARGETS backy_x_cli backy_full_gui RUNTIME DESTINATION bin)

# --------------------------------------------------------------------
# ============== type de build par défaut =============================
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
