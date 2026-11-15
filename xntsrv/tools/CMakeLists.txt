xntsrv_program(xntsrv-service xntsrv-service.cpp)
xntsrv_program(xntsrv-register xntsrv-register.cpp)
xntsrv_program(xntsrv-unregister xntsrv-unregister.cpp)
xntsrv_program(xntsrv-reset xntsrv-reset.cpp)

# Installs a real service and writes to HKLM, so it only runs where that is
# wanted: set XNTSRV_SYSTEM_TESTS=1 (CI does) and use an elevated prompt.
add_test(NAME system COMMAND ${CMAKE_COMMAND}
  -DBIN=$<TARGET_FILE_DIR:xntsrv-register>
  -P ${CMAKE_CURRENT_SOURCE_DIR}/test.cmake)
