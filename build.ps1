<#
.SYNOPSIS
  Builds OpenRayman on Windows using the installed Visual Studio (Build Tools) toolchain.

.EXAMPLE
  .\build.ps1                 # Debug build + tests
  .\build.ps1 -Release        # Optimized build + tests
  .\build.ps1 -NoTests        # Skip running the tests
#>
param(
	[switch]$Release,
	[switch]$NoTests
)

$ErrorActionPreference = "Stop"
$preset = if ($Release) { "windows-release" } else { "windows-debug" }

# Enter the MSVC developer environment once per shell.
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
	$installer = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer"
	$vswhere = Join-Path $installer "vswhere.exe"
	# The developer shell script calls vswhere itself and complains if it isn't on PATH.
	$env:PATH = "$installer;$env:PATH"
	$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
	if (-not $vs) { throw "No Visual Studio with C++ tools found. Install 'Visual Studio Build Tools' with the C++ workload." }
	Import-Module (Join-Path $vs "Common7\Tools\Microsoft.VisualStudio.DevShell.dll")
	Enter-VsDevShell -VsInstallPath $vs -DevCmdArguments "-arch=x64 -host_arch=x64" -SkipAutomaticLocation | Out-Null
}

Push-Location $PSScriptRoot
try {
	cmake --preset $preset
	if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
	cmake --build --preset $preset
	if ($LASTEXITCODE -ne 0) { throw "Build failed" }
	if (-not $NoTests) {
		ctest --preset $preset
		if ($LASTEXITCODE -ne 0) { throw "Tests failed" }
	}
	Write-Host "`nBuilt into build\$preset\bin" -ForegroundColor Green
}
finally {
	Pop-Location
}
