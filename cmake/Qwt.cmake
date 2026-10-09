# Builds Qwt 6.3.0 from the official source archive as a shared library (Qwt::Qwt).
include(FetchContent)

FetchContent_Declare(qwt_src
  URL https://sourceforge.net/projects/qwt/files/qwt/6.3.0/qwt-6.3.0.zip/download
  URL_HASH SHA256=268d8d41974d263014fea13168324452f41e02880db573794f559995a4e11331
  DOWNLOAD_NAME qwt-6.3.0.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  # Qwt ships qmake files only: fetch the sources but do not add_subdirectory() them.
  SOURCE_SUBDIR no-cmake-project
)
FetchContent_MakeAvailable(qwt_src)

find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport Svg Concurrent OpenGL OpenGLWidgets)

file(GLOB QWT_SOURCES CONFIGURE_DEPENDS "${qwt_src_SOURCE_DIR}/src/*.cpp")
file(GLOB QWT_HEADERS CONFIGURE_DEPENDS "${qwt_src_SOURCE_DIR}/src/*.h")
# Exclude the polar plot classes (not needed; qwt_point_polar is kept) and the
# Qt5-only QGL canvas (Qt6 uses qwt_plot_opengl_canvas instead).
list(FILTER QWT_SOURCES EXCLUDE REGEX "/qwt_(polar_[^/]*|plot_glcanvas)\\.cpp$")
list(FILTER QWT_HEADERS EXCLUDE REGEX "/qwt_(polar_[^/]*|plot_glcanvas)\\.h$")

add_library(qwt SHARED ${QWT_SOURCES} ${QWT_HEADERS})
add_library(Qwt::Qwt ALIAS qwt)

set_target_properties(qwt PROPERTIES
  AUTOMOC ON
  DEBUG_POSTFIX d
  VERSION 6.3.0
)
target_include_directories(qwt PUBLIC "${qwt_src_SOURCE_DIR}/src")
target_compile_definitions(qwt PUBLIC QWT_DLL PRIVATE QWT_MAKEDLL)
target_link_libraries(qwt PUBLIC Qt6::Widgets Qt6::PrintSupport Qt6::Svg Qt6::Concurrent
                                 Qt6::OpenGL Qt6::OpenGLWidgets)