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

set(FW netsh advfirewall firewall show rule "name=XNTSrv server (block inbound)")

# Start from a clean machine.
execute_process(COMMAND ${BIN}/xntsrv-reset.exe --yes OUTPUT_QUIET ERROR_QUIET)
expect(1 "cannot be started directly" ${BIN}/xntsrv-service.exe)
expect(1 "cannot be started directly" ${BIN}/xntsrv-service.exe bogus)
expect(1 "" ${FW})

# A service left by an earlier version is cleaned up by register.
execute_process(COMMAND sc create XNTSrv binPath= "${BIN}/xntsrv-service.exe" OUTPUT_QUIET ERROR_QUIET)

# Register.
expect(0 "Service XNTSrv WSL: +installed and running" ${BIN}/xntsrv-register.exe)
expect(0 "RUNNING" sc query XNTSrvWSL)
expect(0 "xntsrv-service.exe\" wsl" sc qc XNTSrvWSL)
expect(1060 "" sc query XNTSrv)
expect(0 "ElementName +REG_SZ +XNTSrv" reg query "${HV}")
expect(0 "xntsrv\\.exe" reg query "${APP}" /ve)
expect(0 "InstallDir" reg query "HKLM\\SOFTWARE\\XNTSrv")
expect(0 "Block.*xntsrv-server.exe|xntsrv-server.exe.*Block" ${FW} verbose)
expect(0 "Service XNTSrv WSL: +installed and running" ${BIN}/xntsrv-register.exe)   # repeatable

# A service the administrator disabled stays disabled across a re-register.
execute_process(COMMAND sc stop XNTSrvWSL OUTPUT_QUIET ERROR_QUIET)
execute_process(COMMAND ${CMAKE_COMMAND} -E sleep 3)
execute_process(COMMAND sc config XNTSrvWSL start= disabled OUTPUT_QUIET ERROR_QUIET)
expect(0 "Service XNTSrv WSL: +installed, left disabled" ${BIN}/xntsrv-register.exe)
execute_process(COMMAND sc config XNTSrvWSL start= auto OUTPUT_QUIET ERROR_QUIET)

# Unregister keeps user settings.
file(MAKE_DIRECTORY "$ENV{LOCALAPPDATA}/XNTSrv/sub")
file(WRITE "$ENV{LOCALAPPDATA}/XNTSrv/sub/keep.txt" "x")
expect(0 "Service XNTSrv WSL: +removed.*Firewall: +rule removed" ${BIN}/xntsrv-unregister.exe)
expect(1060 "" sc query XNTSrvWSL)
expect(1 "" ${FW})
expect(1 "" reg query "${HV}")
expect(1 "" reg query "HKLM\\SOFTWARE\\XNTSrv")
if(NOT EXISTS "$ENV{LOCALAPPDATA}/XNTSrv/sub/keep.txt")
  message("FAIL: xntsrv-unregister deleted user settings")
  math(EXPR failures "${failures}+1")
endif()
expect(0 "Service XNTSrv WSL: +not installed" ${BIN}/xntsrv-unregister.exe)

# Reset asks first, and removes everything when told yes.
expect(0 "installed and running" ${BIN}/xntsrv-register.exe)
expect_input("n" 1 "Continue\\? \\[y/N\\] Aborted" ${BIN}/xntsrv-reset.exe)
expect_input("" 1 "Aborted" ${BIN}/xntsrv-reset.exe)
expect(0 "RUNNING" sc query XNTSrvWSL)
expect_input("y" 0 "Service XNTSrv WSL: +removed.*User settings: +1 folder" ${BIN}/xntsrv-reset.exe)
expect(1 "" reg query "${HV}")
expect(1 "" reg query "${APP}")
expect(1 "" ${FW})
if(EXISTS "$ENV{LOCALAPPDATA}/XNTSrv")
  message("FAIL: xntsrv-reset left user settings behind")
  math(EXPR failures "${failures}+1")
endif()
expect(0 "Service XNTSrv WSL: +not installed.*0 folder" ${BIN}/xntsrv-reset.exe -y)

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
