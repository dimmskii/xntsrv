# Command-line tests for xntsrv, driven through --dry-run so no display is needed.
# Run by ctest; XNTSRV is the built executable.

set(failures 0)
set(S "C:\\x\\srv.exe")
set(G "{01234567-89ab-cdef-0123-456789abcdef}")

# expect(<expected exit code> <regex the output must match> <args>...)
function(expect code pattern)
  execute_process(COMMAND ${XNTSRV} ${ARGN}
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  string(REPLACE "\r" "" all "${out}${err}")
  if(NOT rc EQUAL code OR NOT all MATCHES "${pattern}")
    message("FAIL: xntsrv ${ARGN}\n  exit ${rc} (want ${code})\n  output: ${all}\n  want match: ${pattern}")
    math(EXPR failures "${failures}+1")
    set(failures ${failures} PARENT_SCOPE)
  endif()
endfunction()

set(BASE --dry-run --no-auth --server=${S})

# Defaults: TCP for the server's own use, nothing else.
expect(0 "^C:\\\\x\\\\srv.exe :0 -multiwindow -listen tcp -nolisten hyperv -ac -clipboard -wgl\n$" ${BASE})

# Options in every spelling.
expect(0 "srv.exe :7 -fullscreen -listen tcp "      ${BASE} -d 7 -m fullscreen)
expect(0 "srv.exe :7 -fullscreen -listen tcp "      ${BASE} -d7 -mfullscreen)
expect(0 "srv.exe :2 -listen tcp "                  ${BASE} --display=2 --mode=desktop)
expect(0 "srv.exe :2 -listen tcp "                  ${BASE} --display 2 --mode desktop)
expect(0 " -ac -noclipboard -nowgl -notrayicon -dpi 144\n$" ${BASE} --no-clipboard --no-gl --no-tray --dpi=144)
expect(0 " -wgl -logverbose 3 \"a b\"\n$"           ${BASE} -- -logverbose 3 "a b")
expect(0 "^\"C:\\\\Program Files\\\\X\\\\s.exe\" :0 " --dry-run --no-auth "--server=C:\\Program Files\\X\\s.exe")
expect(0 " -auth [^\n]*XNTsrv\\\\Xauthority -clipboard" --dry-run --server=${S})
expect(0 "srv.exe :0 "                              -n --no-auth --server ${S})

# Errors and informational options.
expect(0 "srv.exe :0 -multiwindow "                 ${BASE} --no-appearance)
expect(0 "srv.exe :0 -multiwindow [^\n]*\n$"        ${BASE} --no-audio)
expect(0 "srv.exe :0 -multiwindow [^\n]*\n$"        ${BASE} -A)
expect(0 "srv.exe :5 "                              ${BASE} -Ad5)

# With the audio component beside the launcher, a second line shows how the
# audio server would be started; -A leaves it out.  (Skipped if something on
# this machine already listens on the audio port: then none is started.)
get_filename_component(xdir "${XNTSRV}" DIRECTORY)
get_filename_component(xname "${XNTSRV}" NAME)
set(adir "${xdir}/audio-test")
file(REMOVE_RECURSE "${adir}")
file(MAKE_DIRECTORY "${adir}/audio/bin")
file(COPY "${XNTSRV}" DESTINATION "${adir}")
file(WRITE "${adir}/audio/bin/pulseaudio.exe" "")
set(XNTSRV_REAL "${XNTSRV}")
set(XNTSRV "${adir}/${xname}")
execute_process(COMMAND netstat -an OUTPUT_VARIABLE ports)
if(ports MATCHES "(127\\.0\\.0\\.1|0\\.0\\.0\\.0):4713 ")
  message("audio-port tests skipped: port 4713 is in use here")
else()
  expect(0 "\n\"?[^\n]*audio\\\\bin\\\\pulseaudio.exe\"? -n \"?--file=[^\n]*XNTsrv\\\\audio\\\\xntsrv.pa\"? [^\n]*--exit-idle-time=-1"
    ${BASE})
  expect(0 "^C:\\\\x\\\\srv.exe [^\n]*\n$"         ${BASE} -A)
endif()
set(XNTSRV "${XNTSRV_REAL}")
file(REMOVE_RECURSE "${adir}")
expect(2 "unrecognized option '--bogus'"            ${BASE} --bogus)
expect(2 "unrecognized option '--listen'"           ${BASE} --listen=tcp)
expect(2 "requires an argument"                     ${BASE} --display)
expect(2 "invalid option -- 'x'"                    ${BASE} -x)
expect(2 "unexpected argument 'stray'"              ${BASE} stray)
expect(2 "invalid value for --mode"                 ${BASE} --mode=rootless)
expect(2 "doesn't allow an argument"                ${BASE} --no-gl=1)
expect(0 "^Usage: xntsrv "                          --help)
expect(0 "^xntsrv [0-9]+\\.[0-9]+\\.[0-9]+\n$"      -V)

if(failures GREATER 0)
  message(FATAL_ERROR "${failures} test(s) failed")
endif()
message("all tests passed")
