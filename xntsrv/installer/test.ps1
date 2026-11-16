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
                   'guest\xntsrv-bridge', 'NOTICE.txt', 'uninstall.exe', 'xkbdata', 'fonts', 'locale') {
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
function Probe([string] $label) {
    $saved = $env:XAUTHORITY
    Write-Output "=== $label"
    Write-Output ("xhost: " + ((& "$dir\xhost.exe" -display 127.0.0.1:0 2>&1 | Out-String).Trim() -replace "`r?`n", ' | '))
    $env:XAUTHORITY = Join-Path $env:TEMP 'no-such-cookie'
    foreach ($d in '127.0.0.1:0', 'localhost:0', '[::1]:0') {
        $o = (& "$dir\xwininfo.exe" -root -display $d 2>&1 | Out-String).Trim() -replace "`r?`n", ' | '
        Write-Output ("no cookie via ${d}: " + $o.Substring(0, [Math]::Min(140, $o.Length)))
    }
    $env:XAUTHORITY = $saved
}
Probe 'helper running'
Stop-Process -Name xntsrv-appearance -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2
Probe 'helper stopped'
& "$dir\xntsrv-appearance.exe" --display=0 &
Start-Sleep -Seconds 4
Probe 'helper started again by hand' 
# The appearance helper, started with the server, has published the Windows
# settings as X resources; read them back the way any X program would.
Check 'appearance helper is running' ($null -ne (Get-Process xntsrv-appearance -ErrorAction SilentlyContinue))
$resources = & "$dir\xrdb.exe" -display 127.0.0.1:0 -query 2>&1 | Out-String
Write-Output $resources.Trim()
Check 'display scale published (Xft.dpi)' ($resources -match 'Xft\.dpi:\s+\d+')
Check 'pointer size published (Xcursor.size)' ($resources -match 'Xcursor\.size:\s+\d+')
Write-Output "what the helper reads from this machine: $((& "$dir\xntsrv-appearance.exe" --print) -join ', ')"
$env:XAUTHORITY = Join-Path $env:TEMP 'no-such-cookie'
$info = & "$dir\xwininfo.exe" -root -display 127.0.0.1:0 2>&1 | Out-String
Check 'a client without the cookie is refused' ($info -notmatch 'Width: \d+')
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
