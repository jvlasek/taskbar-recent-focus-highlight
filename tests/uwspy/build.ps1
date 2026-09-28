param([string]$Configuration = 'Release', [string]$UwpSpyRoot = $env:UWPSPY_ROOT)
$ErrorActionPreference = 'Stop'
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Visual Studio C++ tools not found' }
$bin = Join-Path $PSScriptRoot 'bin'
New-Item -ItemType Directory -Force $bin | Out-Null
$project = Join-Path $PSScriptRoot 'TestChild.vcxproj'
& (Join-Path $vs 'MSBuild\Current\Bin\amd64\MSBuild.exe') $project /p:Configuration=$Configuration /p:Platform=x64 /p:PreferredToolArchitecture=x64 /v:minimal
if ($LASTEXITCODE) { throw 'Child build failed' }
foreach ($name in 'A','B','C','D') { Copy-Item -LiteralPath (Join-Path $bin 'TestChild.exe') -Destination (Join-Path $bin "$name.exe") }

if (!$UwpSpyRoot) { $UwpSpyRoot = Join-Path $PSScriptRoot '..\..\..\UWPSpy\watcher' }
$UwpSpyRoot = (Resolve-Path -LiteralPath $UwpSpyRoot).Path
& (Join-Path $vs 'MSBuild\Current\Bin\amd64\MSBuild.exe') (Join-Path $PSScriptRoot 'IpcFixture.vcxproj') /p:Configuration=$Configuration /p:Platform=x64 /p:PreferredToolArchitecture=x64 "/p:UwpSpyRoot=$UwpSpyRoot" /v:minimal
if ($LASTEXITCODE) { throw 'IPC fixture build failed; build UWPSpy Release/x64 first' }
