# xntsrv.exe is the console program; xntsrvw.exe is the same code built for the
# windows subsystem, for shortcuts and autostart (like python.exe / pythonw.exe).
xntsrv_program(xntsrv xntsrv.cpp)
xntsrv_program(xntsrvw xntsrv.cpp WIN32)

add_test(NAME cli COMMAND ${CMAKE_COMMAND}
  -DXNTSRV=$<TARGET_FILE:xntsrv>
  -P ${CMAKE_CURRENT_SOURCE_DIR}/test.cmake)
add_test(NAME cli-windowed COMMAND ${CMAKE_COMMAND}
  -DXNTSRV=$<TARGET_FILE:xntsrvw>
  -P ${CMAKE_CURRENT_SOURCE_DIR}/test.cmake)
