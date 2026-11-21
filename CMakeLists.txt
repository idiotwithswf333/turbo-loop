cmake_minimum_required(VERSION 3.20)
project(TurboLoop C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Static C runtime on Windows, so the .exe runs on a PC without the Visual C++ redistributable.
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(raylib URL https://github.com/raysan5/raylib/archive/refs/tags/5.5.tar.gz)
FetchContent_MakeAvailable(raylib)

add_executable(turboloop src/main.cpp)
target_link_libraries(turboloop PRIVATE raylib)

if(WIN32)
  set_target_properties(turboloop PROPERTIES WIN32_EXECUTABLE TRUE)  # no console window
  if(MSVC)
    target_link_options(turboloop PRIVATE /ENTRY:mainCRTStartup)      # keep a plain main()
  endif()
endif()
