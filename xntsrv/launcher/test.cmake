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

# Defaults (with the VM pinned so the output does not depend on the machine).
expect(0 "^C:\\\\x\\\\srv.exe :0 -multiwindow -listen hyperv -nolisten tcp -vsockport 106000 -ac -clipboard -wgl\n$"
  ${BASE} --vmid=any)

# --listen / -l in every spelling.
expect(0 " -listen hyperv -nolisten tcp -vmid ${G} -vsockport 106000 " ${BASE} --vmid=${G} --listen=hyperv)
expect(0 " -nolisten hyperv -listen tcp -ac "       ${BASE} --listen=tcp)
expect(0 " -nolisten hyperv -listen tcp -ac "       ${BASE} --listen tcp)
expect(0 " -nolisten hyperv -listen tcp -ac "       ${BASE} -l tcp)
expect(0 " -nolisten hyperv -listen tcp -ac "       ${BASE} -ltcp)
expect(0 " -listen hyperv -listen tcp -vsockport "  ${BASE} --vmid=any -l hyperv,tcp)
expect(0 " -listen hyperv -listen tcp -vsockport "  ${BASE} --vmid=any -l tcp,hyperv)
expect(0 " -nolisten hyperv -nolisten tcp -ac "     ${BASE} -l none)
expect(2 "invalid value for --listen: 'none,tcp'"   ${BASE} -l none,tcp)
expect(2 "invalid value for --listen: 'udp'"        ${BASE} -l udp)
expect(2 "invalid value for --listen: ''"           ${BASE} --listen=)
expect(2 "requires an argument"                     ${BASE} --listen)

# Other options.
expect(0 "srv.exe :7 -fullscreen "                  ${BASE} --vmid=any -d 7 -m fullscreen)
expect(0 "srv.exe :2 -listen hyperv "               ${BASE} --vmid=any --display=2 --mode=desktop)
expect(0 " -vsockport 6000 -ac -noclipboard -nowgl -notrayicon -dpi 144\n$"
  ${BASE} --vmid=any --vsock-port=6000 --no-clipboard --no-gl --no-tray --dpi=144)
expect(0 " -wgl -logverbose 3 \"a b\"\n$"           ${BASE} --vmid=any -- -logverbose 3 "a b")
expect(0 "^\"C:\\\\Program Files\\\\X\\\\s.exe\" :0 " --dry-run --no-auth --vmid=any "--server=C:\\Program Files\\X\\s.exe")
expect(0 " -auth [^\n]*XNTSrv\\\\Xauthority -clipboard" --dry-run --vmid=any --server=${S})
expect(0 "srv.exe :0 "                              -n --no-auth --vmid any --server ${S})

# Errors and informational options.
expect(2 "unrecognized option '--bogus'"            ${BASE} --bogus)
expect(2 "invalid option -- 'x'"                    ${BASE} -x)
expect(2 "unexpected argument 'stray'"              ${BASE} stray)
expect(2 "invalid value for --vmid"                 ${BASE} --vmid=1234)
expect(2 "invalid value for --mode"                 ${BASE} --mode=rootless)
expect(2 "doesn't allow an argument"                ${BASE} --no-gl=1)
expect(0 "srv.exe :0 "                              ${BASE} --vmid=any -v)
expect(0 "srv.exe :0 "                              ${BASE} --vmid=any -nv)
expect(0 "^Usage: xntsrv "                          --help)
expect(0 "^xntsrv [0-9]+\\.[0-9]+\\.[0-9]+\n$"      -V)

if(failures GREATER 0)
  message(FATAL_ERROR "${failures} test(s) failed")
endif()
message("all tests passed")
