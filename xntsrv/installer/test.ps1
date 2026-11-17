# Installs, upgrades and uninstalls XNTsrv silently and checks the result.
# Changes the machine it runs on: needs an elevated prompt, meant for CI.
param([Parameter(Mandatory)] [string] $Setup)
$ErrorActionPreference = 'Stop'

$dir = Join-Path $env:ProgramFiles 'XNTsrv'
$shortcutPath = Join-Path $env:ProgramData 'Microsoft\Windows\Start Menu\Programs\XNTsrv.lnk'
$uninstKey = 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\XNTsrv'
$script:failures = 0

function Check([string] $what, [bool] $ok) {
    if ($ok) { Write-Output "ok    $what" } else { Write-Output "FAIL  $what"; $script:failures++ }
}
function Run([string] $exe, [string[]] $arguments) {
    (Start-Process -FilePath $exe -ArgumentList $arguments -Wait -PassThru).ExitCode
}
function ServiceState { (Get-Service XNTsrvWSL -ErrorAction SilentlyContinue).Status }

$compKey = 'HKLM:\SOFTWARE\XNTsrv\Components'
function Component([string] $name) { (Get-ItemProperty $compKey -ErrorAction SilentlyContinue).$name }

function CheckInstalled([string] $pass) {
    Check "$pass`: service running" ((ServiceState) -eq 'Running')
    Check "$pass`: Session service running" ((Get-Service XNTsrvSession -ErrorAction SilentlyContinue).Status -eq 'Running')
    Check "$pass`: TCP service installed but stopped" ((Get-Service XNTsrvTCP -ErrorAction SilentlyContinue).Status -eq 'Stopped')
    foreach ($f in 'xntsrv.exe', 'xntsrvw.exe', 'xntsrv-server.exe', 'xntsrv-service.exe', 'xntsrv-register.exe',
                   'xntsrv-unregister.exe', 'xntsrv-reset.exe', 'xntsrv-appearance.exe', 'xkbcomp.exe', 'vcruntime140.dll',
                   'theme\gtk-3.0\gtk.css', 'theme\gtk-2.0\gtkrc', 'theme\index.theme', 'NOTICE.txt', 'uninstall.exe', 'xkbdata', 'fonts', 'locale') {
        Check "$pass`: installed $f" (Test-Path (Join-Path $dir $f))
    }
    Check "$pass`: no vcxsrv.exe, xlaunch.exe or installer leftovers" (-not ((Test-Path "$dir\vcxsrv.exe") -or
        (Test-Path "$dir\xlaunch.exe") -or (Test-Path "$dir\`$PLUGINSDIR")))
    Check "$pass`: listed in Apps" ((Get-ItemProperty $uninstKey -ErrorAction SilentlyContinue).DisplayName -eq 'XNTsrv')
    Check "$pass`: publisher is Dmitri Poterianski" ((Get-ItemProperty $uninstKey -ErrorAction SilentlyContinue).Publisher -eq 'Dmitri Poterianski')
    Check "$pass`: firewall rule present" ($null -ne (Get-NetFirewallRule -DisplayName 'XNTsrv server (block inbound)' -ErrorAction SilentlyContinue))
    Check "$pass`: install location registered" ((Get-ItemProperty 'HKLM:\SOFTWARE\XNTsrv').InstallDir -eq $dir)
}

# Which optional components are there, on disk and as remembered for the next upgrade.
function CheckComponents([string] $pass, [bool] $audio, [bool] $guest, [bool] $shortcut) {
    Check "$pass`: audio component $(if ($audio) { 'installed' } else { 'absent' })" ((Test-Path "$dir\audio\bin\pulseaudio.exe") -eq $audio)
    Check "$pass`: guest files $(if ($guest) { 'installed' } else { 'absent' })" ((Test-Path "$dir\guest\xntsrv-bridge") -eq $guest)
    Check "$pass`: Start menu shortcut $(if ($shortcut) { 'present' } else { 'absent' })" ((Test-Path $shortcutPath) -eq $shortcut)
    Check "$pass`: choices remembered" ((Component 'Audio') -eq [int]$audio -and (Component 'Guest') -eq [int]$guest -and
        (Component 'Shortcut') -eq [int]$shortcut)
}

Check 'install exits 0' ((Run $Setup @('/S')) -eq 0)
CheckInstalled 'install'
CheckComponents 'install' $true $true $true

Check 'install without audio and shortcut exits 0' ((Run $Setup @('/S', '/NOAUDIO', '/NOSHORTCUT')) -eq 0)
CheckInstalled 'minimal'
CheckComponents 'minimal' $false $true $false

Check 'upgrade keeping the choices exits 0' ((Run $Setup @('/S')) -eq 0)
CheckComponents 'upgrade' $false $true $false

Check 'upgrade adding them back exits 0' ((Run $Setup @('/S', '/AUDIO', '/SHORTCUT')) -eq 0)
CheckInstalled 'full again'
CheckComponents 'full again' $true $true $true

$lines = @(& "$dir\xntsrv.exe" --dry-run --no-auth)
Check "launcher finds its server: $($lines[0])" ($lines[0] -like "`"$dir\xntsrv-server.exe`" :0 *")
Check "launcher would start the audio server: $($lines[1])" ($lines[1] -like "`"$dir\audio\bin\pulseaudio.exe`" -n *")

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
# The audio server started with it, answers with its cookie, and refuses without.
Check 'audio server is running' ($null -ne (Get-Process pulseaudio -ErrorAction SilentlyContinue))
$pactl = "$dir\audio\bin\pactl.exe"
$env:PULSE_COOKIE = Join-Path $env:LOCALAPPDATA 'XNTsrv\pulse-cookie'
$pinfo = & $pactl -s tcp:127.0.0.1:4713 info 2>&1 | Out-String
Write-Output ($pinfo.Trim() -split "`r?`n" | Select-Object -First 3)
Check 'an audio client with the cookie is served' ($pinfo -match 'Server Name:\s+pulseaudio')
Check 'there is an output to play to' ($pinfo -match 'Default Sink:\s+\S+')
$env:PULSE_COOKIE = Join-Path $env:TEMP 'no-such-cookie'
$pinfo = & $pactl -s tcp:127.0.0.1:4713 info 2>&1 | Out-String
Check 'an audio client without the cookie is refused' ($pinfo -notmatch 'Server Name:')
Remove-Item Env:PULSE_COOKIE
$plog = Join-Path $env:LOCALAPPDATA 'XNTsrv\audio\pulseaudio.log'
if (Test-Path $plog) { Write-Output '--- pulseaudio.log'; Get-Content $plog -Tail 15 }

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
Check 'the audio server ended with it' ($null -eq (Get-Process pulseaudio -ErrorAction SilentlyContinue))
Check 'program files removed' (-not (Test-Path "$dir\xntsrv-server.exe"))
Check 'shortcut removed' (-not (Test-Path $shortcutPath))
Check 'Apps entry removed' (-not (Test-Path $uninstKey))
Check 'firewall rule removed' ($null -eq (Get-NetFirewallRule -DisplayName 'XNTsrv server (block inbound)' -ErrorAction SilentlyContinue))
Check 'registration removed' (-not (Test-Path 'HKLM:\SOFTWARE\XNTsrv'))

if ($script:failures -gt 0) { throw "$($script:failures) installer check(s) failed" }
Write-Output 'all installer checks passed'
# The refused client above left a non-zero exit code behind; the checks decide the result.
exit 0
