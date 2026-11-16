# Installs, upgrades and uninstalls XNTsrv silently and checks the result.
# Changes the machine it runs on: needs an elevated prompt, meant for CI.
param([Parameter(Mandatory)] [string] $Setup)
$ErrorActionPreference = 'Stop'

$dir = Join-Path $env:ProgramFiles 'XNTsrv'
$shortcut = Join-Path $env:ProgramData 'Microsoft\Windows\Start Menu\Programs\XNTsrv.lnk'
$uninstKey = 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\XNTsrv'
$script:failures = 0

function Check([string] $what, [bool] $ok) {
    if ($ok) { Write-Output "ok    $what" } else { Write-Output "FAIL  $what"; $script:failures++ }
}
function Run([string] $exe, [string[]] $arguments) {
    (Start-Process -FilePath $exe -ArgumentList $arguments -Wait -PassThru).ExitCode
}
function ServiceState { (Get-Service XNTsrvWSL -ErrorAction SilentlyContinue).Status }

foreach ($pass in 'install', 'install over existing') {
    Check "$pass exits 0" ((Run $Setup @('/S')) -eq 0)
    Check 'service running' ((ServiceState) -eq 'Running')
    Check 'Session service running' ((Get-Service XNTsrvSession -ErrorAction SilentlyContinue).Status -eq 'Running')
    Check 'TCP service installed but stopped' ((Get-Service XNTsrvTCP -ErrorAction SilentlyContinue).Status -eq 'Stopped')
    foreach ($f in 'xntsrv.exe', 'xntsrvw.exe', 'xntsrv-server.exe', 'xntsrv-service.exe', 'xntsrv-register.exe',
                   'xntsrv-unregister.exe', 'xntsrv-reset.exe', 'xntsrv-appearance.exe', 'xkbcomp.exe', 'vcruntime140.dll',
                   'guest\xntsrv-bridge', 'theme\gtk-3.0\gtk.css', 'theme\gtk-2.0\gtkrc', 'theme\index.theme', 'NOTICE.txt', 'uninstall.exe', 'xkbdata', 'fonts', 'locale') {
        Check "installed $f" (Test-Path (Join-Path $dir $f))
    }
    Check 'no vcxsrv.exe, xlaunch.exe or installer leftovers' (-not ((Test-Path "$dir\vcxsrv.exe") -or
        (Test-Path "$dir\xlaunch.exe") -or (Test-Path "$dir\`$PLUGINSDIR")))
    Check 'Start menu shortcut' (Test-Path $shortcut)
    Check 'listed in Apps' ((Get-ItemProperty $uninstKey -ErrorAction SilentlyContinue).DisplayName -eq 'XNTsrv')
    Check 'publisher is Dmitri Poterianski' ((Get-ItemProperty $uninstKey -ErrorAction SilentlyContinue).Publisher -eq 'Dmitri Poterianski')
    Check 'firewall rule present' ($null -ne (Get-NetFirewallRule -DisplayName 'XNTsrv server (block inbound)' -ErrorAction SilentlyContinue))
    Check 'install location registered' ((Get-ItemProperty 'HKLM:\SOFTWARE\XNTsrv').InstallDir -eq $dir)
}

$line = & "$dir\xntsrv.exe" --dry-run --no-auth
Check "launcher finds its server: $line" ($line -like "`"$dir\xntsrv-server.exe`" :0 *")

# The Session service starts the server for whoever is logged in.  A CI runner
# may have no logged-in session for it to find; then start the server the way
# the Start menu shortcut does.  Either way, talk to it with one of the X
# clients it ships: with the cookie, then without.
foreach ($second in 1..15) {
    if (Get-Process xntsrv-server -ErrorAction SilentlyContinue) { break }
    Start-Sleep -Seconds 1
}
if (Get-Process xntsrv-server -ErrorAction SilentlyContinue) {
    Write-Output 'the Session service started the server'
} else {
    Write-Output 'no server from the Session service (no logged-in session here?); starting it by hand'
    & "$dir\xntsrv.exe"
}
Start-Sleep -Seconds 10
Check 'server is running' ($null -ne (Get-Process xntsrv-server -ErrorAction SilentlyContinue))
$env:XAUTHORITY = Join-Path $env:LOCALAPPDATA 'XNTsrv\Xauthority'
$info = & "$dir\xwininfo.exe" -root -display 127.0.0.1:0 2>&1 | Out-String
Check 'an X client can query the server' ($info -match 'Width: \d+')
# The appearance helper, started with the server, has published the Windows
# settings as X resources; read them back the way any X program would.
Check 'appearance helper is running' ($null -ne (Get-Process xntsrv-appearance -ErrorAction SilentlyContinue))
$resources = & "$dir\xrdb.exe" -display 127.0.0.1:0 -query 2>&1 | Out-String
Write-Output ($resources.Trim() -split "`r?`n" | Select-Object -First 4)
Check 'display scale published (Xft.dpi)' ($resources -match 'Xft\.dpi:\s+\d+')
Check 'pointer size published (Xcursor.size)' ($resources -match 'Xcursor\.size:\s+\d+')
Check 'terminal colours published' ($resources -match '\*color1:\s+#[0-9a-f]{6}' -and $resources -match 'XTerm\*background:\s+#[0-9a-f]{6}')
Check 'pointer scheme published (Xcursor.theme)' ($resources -match 'Xcursor\.theme:\s+XNTsrv')
$themeDir = Join-Path $env:LOCALAPPDATA 'XNTsrv\share\icons\XNTsrv'
Check 'pointer scheme converted into the user data folder' ((Test-Path "$themeDir\index.theme") -and (Get-ChildItem "$themeDir\cursors" -ErrorAction SilentlyContinue).Count -ge 40)
$gtkDir = Join-Path $env:LOCALAPPDATA 'XNTsrv\share\themes\XNTsrv'
Check 'GTK themes written with generated colours' ((Test-Path "$gtkDir\gtk-3.0\gtk.css") -and (Test-Path "$gtkDir\gtk-2.0\gtkrc") -and ((Get-Content "$gtkDir\gtk-3.0\colors.css" -Raw) -match '@define-color xnt_selection #[0-9a-f]{6};') -and ((Get-Content "$gtkDir\gtk-2.0\colors.rc" -Raw) -match 'gtk-color-scheme'))
Check 'terminal font and size published' ($resources -match 'XTerm\*faceSize:\s+\d+' -and $resources -match 'XTerm\*geometry:\s+\d+x\d+')
Write-Output "what the helper reads from this machine: $((& "$dir\xntsrv-appearance.exe" --print) -join ', ')"
# Without the cookie nobody gets in, whichever name this computer is reached by.
$env:XAUTHORITY = Join-Path $env:TEMP 'no-such-cookie'
foreach ($address in '127.0.0.1:0', 'localhost:0', '[::1]:0') {
    $info = & "$dir\xwininfo.exe" -root -display $address 2>&1 | Out-String
    Check "a client without the cookie is refused at $address" ($info -notmatch 'Width: \d+')
}
# The server's own window manager and clipboard connect to it as clients.  If
# they were refused it would give up after about 25 seconds, so see it outlive that.
$started = (Get-Process xntsrv-server -ErrorAction SilentlyContinue | Select-Object -First 1).StartTime
while ($started -and ((Get-Date) - $started).TotalSeconds -lt 40) { Start-Sleep -Seconds 1 }
Check 'server still running after its own clients connected' ($null -ne (Get-Process xntsrv-server -ErrorAction SilentlyContinue))
Write-Output "server product name: $((Get-Item "$dir\xntsrv-server.exe").VersionInfo.ProductName)"
# The log is XNTsrv.0.log from a build of this repository, VCXSrv.0.log from the upstream release.
foreach ($log in 'XNTsrv.0.log', 'VCXSrv.0.log') {
    if (Test-Path "$env:TEMP\$log") { Write-Output "--- $log"; Get-Content "$env:TEMP\$log" -Tail 15 }
}

# _?= makes the uninstaller run in place and wait, so its own file stays behind.
Check 'uninstall exits 0' ((Run "$dir\uninstall.exe" @('/S', "_?=$dir")) -eq 0)
Check 'service removed' ($null -eq (ServiceState))
Check 'the uninstaller closed the running server' ($null -eq (Get-Process xntsrv-server -ErrorAction SilentlyContinue))
Check 'the appearance helper ended with it' ($null -eq (Get-Process xntsrv-appearance -ErrorAction SilentlyContinue))
Check 'program files removed' (-not (Test-Path "$dir\xntsrv-server.exe"))
Check 'shortcut removed' (-not (Test-Path $shortcut))
Check 'Apps entry removed' (-not (Test-Path $uninstKey))
Check 'firewall rule removed' ($null -eq (Get-NetFirewallRule -DisplayName 'XNTsrv server (block inbound)' -ErrorAction SilentlyContinue))
Check 'registration removed' (-not (Test-Path 'HKLM:\SOFTWARE\XNTsrv'))

if ($script:failures -gt 0) { throw "$($script:failures) installer check(s) failed" }
Write-Output 'all installer checks passed'
# The refused client above left a non-zero exit code behind; the checks decide the result.
exit 0
