# Builds the XNTSrv Windows programs.  The Linux-side bridge has its own
# Makefile in bridge/.
cmake_minimum_required(VERSION 3.20)
project(xntsrv LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# All programs land in one folder; they locate each other by sitting side by side.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# xntsrv_program(<name> <source> [WIN32])
function(xntsrv_program name source)
  add_executable(${name} ${ARGN} ${source})
  target_link_libraries(${name} PRIVATE bcrypt shell32 ole32 oleaut32 advapi32 user32 ws2_32 iphlpapi)
  if(MSVC)
    target_compile_options(${name} PRIVATE /W4 /WX /utf-8)
    # Static C runtime: single files with no redistributable to install.
    set_property(TARGET ${name} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  else()
    target_compile_options(${name} PRIVATE -Wall -Wextra -Werror)
    target_link_options(${name} PRIVATE -municode -static)
  endif()
endfunction()

enable_testing()
add_subdirectory(launcher)
add_subdirectory(tools)
