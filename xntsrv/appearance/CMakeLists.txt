xntsrv_program(xntsrv-appearance xntsrv-appearance.cpp cursors-win.cpp xcursor.cpp xclient.cpp publish.cpp)

# What it reads from this machine; the X side is tested from Linux by
# test/publish-test.cpp and, end to end, by the installer test.
add_test(NAME appearance-print COMMAND xntsrv-appearance --print)
set_tests_properties(appearance-print PROPERTIES
  PASS_REGULAR_EXPRESSION "dpi=[0-9]+[\r\n]+cursor=[0-9]+[\r\n]+dark=[01][\r\n]+font=.*[*]color15: #[0-9a-f]+.*XTerm[*]faceSize: [0-9]+.*XTerm[*]geometry: [0-9]+x[0-9]+")

# Convert this machine's pointer scheme and check the files it produces.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME appearance-cursors COMMAND ${CMAKE_COMMAND}
    -DEXE=$<TARGET_FILE:xntsrv-appearance> -DPYTHON=${Python3_EXECUTABLE}
    -DCHECK=${CMAKE_CURRENT_SOURCE_DIR}/test/check-xcursor.py -DOUT=${CMAKE_CURRENT_BINARY_DIR}/cursor-theme
    -P ${CMAKE_CURRENT_SOURCE_DIR}/test/cursors.cmake)
endif()
