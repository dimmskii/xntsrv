# System test for xntsrv-register, xntsrv-unregister, xntsrv-reset and the
# service.  It changes the machine it runs on, so it is opt-in.

if(NOT "$ENV{XNTSRV_SYSTEM_TESTS}" STREQUAL "1")
  message("skipped: set XNTSRV_SYSTEM_TESTS=1 to run (installs a service, writes to HKLM)")
  return()
endif()

set(failures 0)
set(HV "HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Virtualization\\GuestCommunicationServices\\{00019e10-facb-11e6-bd58-64006a7986d3}")
set(APP "HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\xntsrv.exe")

# expect(<exit code> <regex the output must match> <command>...)
function(expect code pattern)
  execute_process(COMMAND ${ARGN} RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  string(REPLACE "\r" "" all "${out}${err}")
  if(NOT rc EQUAL code OR NOT all MATCHES "${pattern}")
    message("FAIL: ${ARGN}\n  exit ${rc} (want ${code})\n  output: ${all}\n  want match: ${pattern}")
    math(EXPR failures "${failures}+1")
    set(failures ${failures} PARENT_SCOPE)
  endif()
endfunction()

# Like expect(), feeding one line to the program's standard input.
function(expect_input line code pattern)
  file(WRITE ${BIN}/answer.txt "${line}\n")
  execute_process(COMMAND ${ARGN} INPUT_FILE ${BIN}/answer.txt
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  string(REPLACE "\r" "" all "${out}${err}")
  if(NOT rc EQUAL code OR NOT all MATCHES "${pattern}")
    message("FAIL: ${ARGN} <<< ${line}\n  exit ${rc} (want ${code})\n  output: ${all}\n  want match: ${pattern}")
    math(EXPR failures "${failures}+1")
    set(failures ${failures} PARENT_SCOPE)
  endif()
endfunction()

set(DRY ${BIN}/xntsrv.exe --dry-run --no-auth --server=C:\\x\\srv.exe)
set(PIPE powershell -NoProfile -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_LIST_DIR}/pipe-test.ps1)

# Start from a clean machine.
execute_process(COMMAND ${BIN}/xntsrv-reset.exe --yes OUTPUT_QUIET ERROR_QUIET)

# Nothing installed: the launcher refuses to start a server no client can reach.
expect(1 "cannot identify the WSL VM: " ${DRY})
expect(1 "cannot be run directly" ${BIN}/xntsrv-service.exe)
expect(1 "" ${PIPE})

# Register.
expect(0 "Service: +installed and running" ${BIN}/xntsrv-register.exe)
expect(0 "RUNNING" sc query XNTSrv)
expect(0 "ElementName +REG_SZ +XNTSrv" reg query "${HV}")
expect(0 "xntsrv\\.exe" reg query "${APP}" /ve)
expect(0 "InstallDir" reg query "HKLM\\SOFTWARE\\XNTSrv")
expect(0 "Service: +installed and running" ${BIN}/xntsrv-register.exe)   # repeatable

# The service answers on its pipe, repeatedly.  CI has no WSL VM, so the answer
# is an empty line and the launcher still refuses to start.
expect(0 "reply=\\[\\]" ${PIPE})
expect(0 "reply=\\[\\]" ${PIPE})
expect(1 "cannot identify the WSL VM: " ${DRY})

# Unregister keeps user settings.
file(MAKE_DIRECTORY "$ENV{LOCALAPPDATA}/XNTSrv/sub")
file(WRITE "$ENV{LOCALAPPDATA}/XNTSrv/sub/keep.txt" "x")
expect(0 "Service: +removed" ${BIN}/xntsrv-unregister.exe)
expect(1 "" reg query "${HV}")
expect(1 "" reg query "HKLM\\SOFTWARE\\XNTSrv")
expect(1 "" ${PIPE})
if(NOT EXISTS "$ENV{LOCALAPPDATA}/XNTSrv/sub/keep.txt")
  message("FAIL: xntsrv-unregister deleted user settings")
  math(EXPR failures "${failures}+1")
endif()
expect(0 "Service: +not installed" ${BIN}/xntsrv-unregister.exe)

# Reset asks first, and removes everything when told yes.
expect(0 "installed and running" ${BIN}/xntsrv-register.exe)
expect_input("n" 1 "Continue\\? \\[y/N\\] Aborted" ${BIN}/xntsrv-reset.exe)
expect_input("" 1 "Aborted" ${BIN}/xntsrv-reset.exe)
expect(0 "RUNNING" sc query XNTSrv)
expect_input("y" 0 "Service: +removed.*User settings: +1 folder" ${BIN}/xntsrv-reset.exe)
expect(1 "" reg query "${HV}")
expect(1 "" reg query "${APP}")
if(EXISTS "$ENV{LOCALAPPDATA}/XNTSrv")
  message("FAIL: xntsrv-reset left user settings behind")
  math(EXPR failures "${failures}+1")
endif()
expect(0 "Service: +not installed.*0 folder" ${BIN}/xntsrv-reset.exe -y)

# Argument handling.
foreach(tool register unregister reset)
  expect(0 "^Usage: xntsrv-${tool} " ${BIN}/xntsrv-${tool}.exe --help)
  expect(0 "^xntsrv-${tool} [0-9.]+\n$" ${BIN}/xntsrv-${tool}.exe -V)
  expect(2 "unrecognized argument '--bogus'" ${BIN}/xntsrv-${tool}.exe --bogus)
endforeach()

file(REMOVE ${BIN}/answer.txt)
if(failures GREATER 0)
  message(FATAL_ERROR "${failures} test(s) failed")
endif()
message("all system tests passed")
