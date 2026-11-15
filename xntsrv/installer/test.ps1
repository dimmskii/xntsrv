# Installs, upgrades and uninstalls XNTSrv silently and checks the result.
# Changes the machine it runs on: needs an elevated prompt, meant for CI.
param([Parameter(Mandatory)] [string] $Setup)
$ErrorActionPreference = 'Stop'

$dir = Join-Path $env:ProgramFiles 'XNTSrv'
$shortcut = Join-Path $env:ProgramData 'Microsoft\Windows\Start Menu\Programs\XNTSrv.lnk'
$uninstKey = 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\XNTSrv'
$script:failures = 0

function Check([string] $what, [bool] $ok) {
    if ($ok) { Write-Output "ok    $what" } else { Write-Output "FAIL  $what"; $script:failures++ }
}
function Run([string] $exe, [string[]] $arguments) {
    (Start-Process -FilePath $exe -ArgumentList $arguments -Wait -PassThru).ExitCode
}
function ServiceState { (Get-Service XNTSrv -ErrorAction SilentlyContinue).Status }

foreach ($pass in 'install', 'install over existing') {
    Check "$pass exits 0" ((Run $Setup @('/S')) -eq 0)
    Check 'service running' ((ServiceState) -eq 'Running')
    foreach ($f in 'xntsrv.exe', 'xntsrvw.exe', 'xntsrv-server.exe', 'xntsrv-service.exe', 'xntsrv-register.exe',
                   'xntsrv-unregister.exe', 'xntsrv-reset.exe', 'xkbcomp.exe', 'vcruntime140.dll',
                   'guest\xntsrv-bridge', 'NOTICE.txt', 'uninstall.exe', 'xkbdata', 'fonts', 'locale') {
        Check "installed $f" (Test-Path (Join-Path $dir $f))
    }
    Check 'no vcxsrv.exe, xlaunch.exe or installer leftovers' (-not ((Test-Path "$dir\vcxsrv.exe") -or
        (Test-Path "$dir\xlaunch.exe") -or (Test-Path "$dir\`$PLUGINSDIR")))
    Check 'Start menu shortcut' (Test-Path $shortcut)
    Check 'listed in Apps' ((Get-ItemProperty $uninstKey -ErrorAction SilentlyContinue).DisplayName -eq 'XNTSrv')
    Check 'install location registered' ((Get-ItemProperty 'HKLM:\SOFTWARE\XNTSrv').InstallDir -eq $dir)
}

$line = & "$dir\xntsrv.exe" --dry-run --vmid=any --no-auth
Check "launcher finds its server: $line" ($line -like "`"$dir\xntsrv-server.exe`" :0 *")

# _?= makes the uninstaller run in place and wait, so its own file stays behind.
Check 'uninstall exits 0' ((Run "$dir\uninstall.exe" @('/S', "_?=$dir")) -eq 0)
Check 'service removed' ($null -eq (ServiceState))
Check 'program files removed' (-not (Test-Path "$dir\xntsrv-server.exe"))
Check 'shortcut removed' (-not (Test-Path $shortcut))
Check 'Apps entry removed' (-not (Test-Path $uninstKey))
Check 'registration removed' (-not (Test-Path 'HKLM:\SOFTWARE\XNTSrv'))

if ($script:failures -gt 0) { throw "$($script:failures) installer check(s) failed" }
Write-Output 'all installer checks passed'
