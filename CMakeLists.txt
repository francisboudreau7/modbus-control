cmake_minimum_required(VERSION 3.25)

# vcpkg toolchain: pass -DCMAKE_TOOLCHAIN_FILE=... or set VCPKG_ROOT to your vcpkg checkout.
if(NOT CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "")
  else()
    message(FATAL_ERROR "Set VCPKG_ROOT to your vcpkg checkout (or pass -DCMAKE_TOOLCHAIN_FILE=...)")
  endif()
endif()

project(ModbusControl VERSION 1.2.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt is a prebuilt install, not a vcpkg package. Override with -DQT_ROOT=... or the QT_ROOT env var.
if(NOT QT_ROOT AND DEFINED ENV{QT_ROOT})
  set(QT_ROOT "$ENV{QT_ROOT}")
endif()
if(QT_ROOT)
  list(PREPEND CMAKE_PREFIX_PATH "${QT_ROOT}")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets PrintSupport Svg Concurrent Network Test)
qt_standard_project_setup()

find_package(libmodbus CONFIG REQUIRED)

# All executables and DLLs land in one directory so they can be run in place.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Warning/conformance flags for our own targets (not third-party code such as Qwt).
add_library(mc_warnings INTERFACE)
if(MSVC)
  target_compile_options(mc_warnings INTERFACE /W4 /permissive- /utf-8)
endif()

include(cmake/Qwt.cmake)

enable_testing()
add_subdirectory(src/core)
add_subdirectory(src/app)
add_subdirectory(server)
add_subdirectory(tests)

# Icon renderer for tools/make_icons.ps1; not built by default.
add_executable(render_icon EXCLUDE_FROM_ALL tools/render_icon.cpp)
target_link_libraries(render_icon PRIVATE Qt6::Gui Qt6::Svg mc_warnings)

# Install rules: `cmake --install build/<preset> --prefix <dir>` produces a self-contained folder.
include(cmake/Deploy.cmake)