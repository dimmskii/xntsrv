cmake_minimum_required(VERSION 3.20)
project(xntsrv-launcher LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# xntsrv.exe is the console program; xntsrvw.exe is the same code built for the
# windows subsystem, for shortcuts and autostart (like python.exe / pythonw.exe).
add_executable(xntsrv xntsrv.cpp)
add_executable(xntsrvw WIN32 xntsrv.cpp)

foreach(target xntsrv xntsrvw)
  target_link_libraries(${target} PRIVATE bcrypt shell32 ole32 user32)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX /utf-8)
    # Static C runtime: a single file with no redistributable to install.
    set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
    target_link_options(${target} PRIVATE -municode -static)
  endif()
endforeach()

enable_testing()
add_test(NAME cli COMMAND ${CMAKE_COMMAND}
  -DXNTSRV=$<TARGET_FILE:xntsrv>
  -DEMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}
  -P ${CMAKE_CURRENT_SOURCE_DIR}/test.cmake)
add_test(NAME cli-windowed COMMAND ${CMAKE_COMMAND}
  -DXNTSRV=$<TARGET_FILE:xntsrvw>
  -DEMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}
  -P ${CMAKE_CURRENT_SOURCE_DIR}/test.cmake)
