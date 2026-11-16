xntsrv_program(xntsrv-appearance xntsrv-appearance.cpp xclient.cpp publish.cpp)

# What it reads from this machine; the X side is tested from Linux by
# test/publish-test.cpp and, end to end, by the installer test.
add_test(NAME appearance-print COMMAND xntsrv-appearance --print)
set_tests_properties(appearance-print PROPERTIES
  PASS_REGULAR_EXPRESSION "dpi=[0-9]+[\r\n]+cursor=[0-9]+[\r\n]+dark=[01][\r\n]+font=.*[*]color15: #[0-9a-f]+")
