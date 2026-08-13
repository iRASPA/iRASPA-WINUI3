param(
  [Parameter(Mandatory=$true)][string]$OutDir,
  [Parameter(Mandatory=$true)][string]$GenDir
)
$ErrorActionPreference = "Stop"
$utf8 = New-Object System.Text.UTF8Encoding $false
$out = [IO.Path]::GetFullPath($OutDir.TrimEnd('\','/'))
$gen = [IO.Path]::GetFullPath($GenDir.TrimEnd('\','/'))
$names = Get-ChildItem (Join-Path $gen "*.xaml") -File | Sort-Object Name | Select-Object -ExpandProperty Name
if (-not $names) { throw "no .xaml found in $gen" }
foreach ($n in $names) {
  Remove-Item (Join-Path $out ([IO.Path]::ChangeExtension($n, "xbf"))) -Force -ErrorAction SilentlyContinue
}
foreach ($n in $names) {
  $p = Join-Path $gen $n
  $b = [IO.File]::ReadAllBytes($p)
  $t = if ($b.Length -ge 3 -and $b[0] -eq 0xEF -and $b[1] -eq 0xBB -and $b[2] -eq 0xBF) {
    $utf8.GetString($b, 3, $b.Length - 3)
  } else { $utf8.GetString($b) }
  [IO.File]::WriteAllText((Join-Path $out $n), $t, $utf8)
  Write-Host "deployed $n ($((Get-Item (Join-Path $out $n)).Length) bytes)"
}
$layout = ($names -join "`r`n") + "`r`n"
[IO.File]::WriteAllText((Join-Path $out "layout.resfiles"), $layout, [Text.Encoding]::ASCII)

# The WinUI framework PRI (generic.xaml/theme resources for templated controls
# like TreeView) must be merged into resources.pri for self-contained unpackaged
# apps; otherwise XAML fail-fasts resolving ms-appx:///Microsoft.UI.Xaml/Themes/generic.xaml.
$frameworkPriIndex = ""
$frameworkPri = Join-Path $out "Microsoft.UI.Xaml.Controls.pri"
if (Test-Path $frameworkPri) {
  $frameworkPriIndex = @"

  <index root="\" startIndexAt="Microsoft.UI.Xaml.Controls.pri">
    <default>
      <qualifier name="Language" value="en-US" />
    </default>
    <indexer-config type="PRI" />
  </index>
"@
} else {
  Write-Warning "framework pri not found at $frameworkPri; templated controls will crash"
}
$priconfig = @"
<?xml version="1.0" encoding="utf-8"?>
<resources targetOsVersion="10.0.0" majorVersion="1">
  <index root="\" startIndexAt="layout.resfiles">
    <default>
      <qualifier name="Language" value="en-US" />
    </default>
    <indexer-config type="RESFILES" qualifierDelimiter="." />
  </index>$frameworkPriIndex
</resources>
"@
[IO.File]::WriteAllText((Join-Path $out "priconfig.xml"), $priconfig, $utf8)
$sdk = $env:WindowsSdkDir
if (-not $sdk) { $sdk = "C:\Program Files (x86)\Windows Kits\10\" }
$makepri = Get-ChildItem (Join-Path $sdk "bin") -Recurse -Filter makepri.exe -ErrorAction SilentlyContinue |
  Where-Object { $_.FullName -match '\\x64\\makepri.exe$' } |
  Sort-Object FullName -Descending |
  Select-Object -First 1 -ExpandProperty FullName
if ($makepri) {
  Push-Location $out
  & $makepri new /cf priconfig.xml /pr . /of resources.pri /o | Out-Null
  Copy-Item resources.pri iRASPA.pri -Force
  Pop-Location
  Write-Host "DeployUnpackagedXaml: resources.pri=$((Get-Item (Join-Path $out 'resources.pri')).Length) bytes"
} else {
  Write-Warning "makepri.exe not found"
}