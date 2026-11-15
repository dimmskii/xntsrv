cmake_minimum_required(VERSION 3.20)
project(xntsrv-launcher LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(xntsrv WIN32 xntsrv.cpp)
target_link_libraries(xntsrv PRIVATE bcrypt shell32 ole32 advapi32 user32)

if(MSVC)
  target_compile_options(xntsrv PRIVATE /W4 /WX /utf-8)
  # Static C runtime: the launcher is a single file with no redistributable to install.
  set_property(TARGET xntsrv PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
  target_compile_options(xntsrv PRIVATE -Wall -Wextra -Werror)
  target_link_options(xntsrv PRIVATE -municode -static)
endif()

enable_testing()
add_test(NAME cli COMMAND ${CMAKE_COMMAND}
  -DXNTSRV=$<TARGET_FILE:xntsrv>
  -DEMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}
  -P ${CMAKE_CURRENT_SOURCE_DIR}/test.cmake)
