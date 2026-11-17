# Assembles the folders the installer packages, one per component:
#
#   core\    the X server, the XNTsrv programs and the GTK themes
#   audio\   PulseAudio, started with the X server (installed as audio\)
#   guest\   the Linux-side bridge (installed as guest\)
#
# The X server comes as an installer made by upstream's own NSIS script:
# either one built from this repository by the server workflow
# (-ServerPackage), or, when there is none, the upstream VcXsrv release,
# downloaded at a pinned version and verified.  Either way it is extracted,
# and the server ends up as xntsrv-server.exe.
#
# SPDX-License-Identifier: MIT
param(
    [Parameter(Mandatory)] [string] $BinDir,   # folder with the built XNTsrv .exe files
    [Parameter(Mandatory)] [string] $Bridge,   # the built Linux xntsrv-bridge binary
    [Parameter(Mandatory)] [string] $OutDir,   # folder to create
    [string] $ServerPackage = '',              # installer built by the server workflow
    [string] $ServerCommit = ''                # the commit that installer was built from
)
$ErrorActionPreference = 'Stop'

$UpstreamVersion = '21.1.16.1'
$UpstreamUrl = "https://github.com/marchaesen/vcxsrv/releases/download/$UpstreamVersion/vcxsrv-64.$UpstreamVersion.installer.exe"
$UpstreamSha256 = 'df7fed8f49665d0592528ab6be9d07111ea73c6848283d128b77690e05b8f90b'

# PulseAudio for Windows, built by the pulseaudio-win32 project.
$AudioRelease = 'v5'
$AudioUrl = "https://github.com/pgaskin/pulseaudio-win32/releases/download/$AudioRelease/pulseaudio.zip"
$AudioSha256 = '13b54a7fd79e96b06bcaba4443bdafd6a5093cf0354e9cf7fdcf765c6c7aa946'

if (Test-Path $OutDir) { Remove-Item -Recurse -Force $OutDir }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$Root = $OutDir
$OutDir = Join-Path $Root 'core'
New-Item -ItemType Directory -Force $OutDir | Out-Null

if ($ServerPackage) {
    $download = (Resolve-Path $ServerPackage).Path
    $origin = @"
xntsrv-server.exe and the files installed alongside it were built from the
VcXsrv source tree in the repository above, at commit $ServerCommit.
"@
    Write-Output "Server: built from source ($ServerCommit)"
} else {
    $download = Join-Path ([IO.Path]::GetTempPath()) "vcxsrv-$UpstreamVersion.installer.exe"
    Invoke-WebRequest -Uri $UpstreamUrl -OutFile $download
    $actual = (Get-FileHash -Algorithm SHA256 $download).Hash.ToLower()
    if ($actual -ne $UpstreamSha256) {
        throw "Upstream installer checksum mismatch: got $actual, expected $UpstreamSha256"
    }
    $origin = @"
xntsrv-server.exe and the files installed alongside it are the unmodified
VcXsrv $UpstreamVersion release, with vcxsrv.exe renamed:
  $UpstreamUrl
Its source code for this release is at:
  https://github.com/marchaesen/vcxsrv/tree/$UpstreamVersion
"@
    Write-Output "Server: upstream release $UpstreamVersion"
}

& 7z x -y "-o$OutDir" $download | Out-Null
if ($LASTEXITCODE -ne 0) { throw "7z failed with exit code $LASTEXITCODE" }

# Installer internals, and upstream programs that only work under the old name.
Remove-Item -Recurse -Force (Join-Path $OutDir '$PLUGINSDIR')
Remove-Item -Force (Join-Path $OutDir 'uninstall.exe')
Remove-Item -Force (Join-Path $OutDir 'xlaunch.exe')   # starts "vcxsrv.exe" by name
# Upstream's X0.hosts trusts "localhost", which lets any program on this
# computer connect without the authentication cookie.  Ship it with no hosts.
Set-Content -Encoding ascii (Join-Path $OutDir 'X0.hosts') "# Hosts that may connect without X authentication. XNTsrv lists none."

# A build from this repository already names its server xntsrv-server.exe.
if (Test-Path (Join-Path $OutDir 'vcxsrv.exe')) {
    Rename-Item (Join-Path $OutDir 'vcxsrv.exe') 'xntsrv-server.exe'
}
if (-not (Test-Path (Join-Path $OutDir 'xntsrv-server.exe'))) { throw 'The server package contains no server executable' }

foreach ($name in 'xntsrv', 'xntsrvw', 'xntsrv-appearance', 'xntsrv-service', 'xntsrv-register', 'xntsrv-unregister', 'xntsrv-reset') {
    Copy-Item (Join-Path $BinDir "$name.exe") $OutDir
}

# The static files of the GTK themes; the appearance helper adds the colours.
Copy-Item -Recurse (Join-Path $PSScriptRoot '..\appearance\theme') (Join-Path $OutDir 'theme')

# The Linux-side forwarder, to be copied into the guest from /mnt/c.
New-Item -ItemType Directory -Force (Join-Path $Root 'guest') | Out-Null
Copy-Item $Bridge (Join-Path $Root 'guest\xntsrv-bridge')

# The audio server: only its programs and modules.  XNTsrv's launcher gives
# it its whole configuration, so the sample configuration is left out.
$zip = Join-Path ([IO.Path]::GetTempPath()) "pulseaudio-win32-$AudioRelease.zip"
Invoke-WebRequest -Uri $AudioUrl -OutFile $zip
$actual = (Get-FileHash -Algorithm SHA256 $zip).Hash.ToLower()
if ($actual -ne $AudioSha256) {
    throw "PulseAudio download checksum mismatch: got $actual, expected $AudioSha256"
}
$unpacked = Join-Path ([IO.Path]::GetTempPath()) "pulseaudio-win32-$AudioRelease"
if (Test-Path $unpacked) { Remove-Item -Recurse -Force $unpacked }
Expand-Archive -Path $zip -DestinationPath $unpacked
New-Item -ItemType Directory -Force (Join-Path $Root 'audio') | Out-Null
Copy-Item -Recurse (Join-Path $unpacked 'pulseaudio\bin') (Join-Path $Root 'audio\bin')
if (-not (Test-Path (Join-Path $Root 'audio\bin\pulseaudio.exe'))) { throw 'The PulseAudio download contains no pulseaudio.exe' }

@"
XNTsrv
https://github.com/dimmskii/xntsrv

$origin
VcXsrv is licensed under the GNU GPL version 3 and the X11/MIT family of
licences.

The xntsrv*.exe programs and guest\xntsrv-bridge are part of XNTsrv and are
MIT licensed. Their source code is in the repository above.

audio\ (when installed) is PulseAudio 15.0 as built for Windows by the
pulseaudio-win32 project, release $AudioRelease, unmodified:
  $AudioUrl
PulseAudio is licensed under the GNU LGPL version 2.1 or later; its source
code is at https://gitlab.freedesktop.org/pulseaudio/pulseaudio (tag v15.0),
and the build recipe at https://github.com/pgaskin/pulseaudio-win32. The
libraries it comes with keep their own licences (LGPL, BSD and MIT family).
"@ | Set-Content -Encoding utf8 (Join-Path $OutDir 'NOTICE.txt')

Write-Output "Staged in $Root (core, audio, guest)"
