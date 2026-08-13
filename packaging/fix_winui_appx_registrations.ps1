# Fix WAP AppxManifest WinRT registrations for self-contained Windows App SDK.
# WinMD harvesting maps many WASDK types to the wrong InProcessServer Path
# (e.g. AppWindow -> Microsoft.UI.dll instead of Microsoft.UI.Windowing.dll,
# XamlControlsResources -> Microsoft.UI.Xaml.dll instead of Controls.dll).
# Packaged activation then fails with CLASS_E_CLASSNOTAVAILABLE (0x80040111),
# surfaced as Microsoft.UI.Xaml.dll / 0xc000027b. Replace harvested WASDK
# servers with the official package.appxfragment registrations.
param(
  [Parameter(Mandatory = $true)][string]$ManifestPath,
  [Parameter(Mandatory = $true)][string]$PackagesDir
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path $ManifestPath)) { throw "manifest missing: $ManifestPath" }
if (-not (Test-Path $PackagesDir)) { throw "packages dir missing: $PackagesDir" }

$fragmentPaths = @(Get-ChildItem -Path $PackagesDir -Directory |
  ForEach-Object { Join-Path $_.FullName 'runtimes-framework\package.appxfragment' } |
  Where-Object { Test-Path $_ })
if ($fragmentPaths.Count -eq 0) {
  throw "no package.appxfragment files under $PackagesDir"
}

[xml]$manifest = Get-Content -Path $ManifestPath -Raw
$ns = 'http://schemas.microsoft.com/appx/manifest/foundation/windows10'
$nsmgr = New-Object System.Xml.XmlNamespaceManager($manifest.NameTable)
$nsmgr.AddNamespace('m', $ns)

$pkg = $manifest.SelectSingleNode('/m:Package', $nsmgr)
if (-not $pkg) { throw 'Package node not found' }

$extRoot = $pkg.SelectSingleNode('./m:Extensions', $nsmgr)
if (-not $extRoot) {
  $extRoot = $manifest.CreateElement('Extensions', $ns)
  $null = $pkg.AppendChild($extRoot)
}

$serversToInject = New-Object System.Collections.Generic.List[System.Xml.XmlElement]
$winSdkDlls = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)

foreach ($fragmentPath in $fragmentPaths) {
  [xml]$fragment = Get-Content -Path $fragmentPath -Raw
  $fnsmgr = New-Object System.Xml.XmlNamespaceManager($fragment.NameTable)
  $fnsmgr.AddNamespace('m', $ns)
  foreach ($server in $fragment.SelectNodes('//m:InProcessServer', $fnsmgr)) {
    $path = $server.SelectSingleNode('./m:Path', $fnsmgr)
    if (-not $path -or [string]::IsNullOrWhiteSpace($path.InnerText)) { continue }
    $null = $winSdkDlls.Add($path.InnerText.Trim())
    $null = $serversToInject.Add($server)
  }
}

if ($serversToInject.Count -eq 0) { throw 'no InProcessServer entries found in fragments' }

# Drop harvested servers we are about to replace from the official fragments.
$toRemove = @()
foreach ($ext in $extRoot.SelectNodes('./m:Extension[@Category="windows.activatableClass.inProcessServer"]', $nsmgr)) {
  $path = $ext.SelectSingleNode('./m:InProcessServer/m:Path', $nsmgr)
  if ($path -and $winSdkDlls.Contains($path.InnerText.Trim())) {
    $toRemove += $ext
  }
}
foreach ($ext in $toRemove) { $null = $extRoot.RemoveChild($ext) }

foreach ($server in $serversToInject) {
  $ext = $manifest.CreateElement('Extension', $ns)
  $null = $ext.SetAttribute('Category', 'windows.activatableClass.inProcessServer')
  $imported = $manifest.ImportNode($server, $true)
  $null = $ext.AppendChild($imported)
  $null = $extRoot.AppendChild($ext)
}

$manifest.Save($ManifestPath)
Write-Host ("Injected {0} WASDK InProcessServer registrations from {1} fragments ({2} DLLs) into {3}" -f `
  $serversToInject.Count, $fragmentPaths.Count, $winSdkDlls.Count, $ManifestPath)
