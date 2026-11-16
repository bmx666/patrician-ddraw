param(
    [Parameter(Mandatory)]
    [string]$ReferenceDirectory,
    [string]$Executable = (Join-Path $PSScriptRoot '..\build\Release\original_sgl_oracle.exe'),
    [string]$ReplacementLibrary = (Join-Path $PSScriptRoot '..\build\Release\ddraw_Dll.dll'),
    [string]$StretchSourcePath,
    [string]$IntermediateSurfacePath,
    [string]$DestinationBeforePath,
    [string]$ClipRectanglesPath,
    [string]$StretchParametersPath,
    [switch]$ScreenReference
)

$ErrorActionPreference = 'Stop'
$reference = (Resolve-Path -LiteralPath $ReferenceDirectory).Path
$expected = @{
    'Patrician3.exe' = 'FE436DC5F8ADDC4D3AA437EB48A8BC6415D9BA5577C1027D06DB9BB915EDDECE'
    'ddraw_Dll.dll' = '694497DEDDB53FAA915007CF088AD1CD119DCF45068F04498CFE1B1D4292901C'
    'ddraw.dll' = '0D96FF5701EDCD98D5DD90988CB545C0649FE64B25FE008AB119A651510F1A0F'
    'archiver.dll' = '044E6D659F41B86A00C5429F66F127B7BD01012F39DFF008FAF4EBD6887BD114'
    'p2arch0_eng.cpr' = '6EBD9AA5A42C496316DD2D137EA99727D200CA557808C15D7EC1B473D4079009'
}
foreach ($name in $expected.Keys) {
    $path = Join-Path $reference $name
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $expected[$name]) {
        throw "Unexpected reference file: $path"
    }
}

if ($ScreenReference) {
    & (Join-Path $PSScriptRoot '..\build\Release\screen_reference.exe') $reference
} else {
    $replacement = (Resolve-Path -LiteralPath $ReplacementLibrary).Path
    $arguments = @($reference, $replacement)
    if (($DestinationBeforePath -or $ClipRectanglesPath) -and -not $StretchSourcePath) {
        throw 'DestinationBeforePath and ClipRectanglesPath require StretchSourcePath.'
    }
    if ([bool]$DestinationBeforePath -ne [bool]$ClipRectanglesPath) {
        throw 'DestinationBeforePath and ClipRectanglesPath must be provided together.'
    }
    if ($DestinationBeforePath -and $IntermediateSurfacePath) {
        throw 'Captured pre-stretch and post-stretch surfaces are separate oracle modes.'
    }
    if ($StretchParametersPath -and -not ($StretchSourcePath -and $DestinationBeforePath -and $ClipRectanglesPath)) {
        throw 'StretchParametersPath requires StretchSourcePath, DestinationBeforePath and ClipRectanglesPath.'
    }
    if ($IntermediateSurfacePath -and -not $StretchSourcePath) {
        throw 'IntermediateSurfacePath requires StretchSourcePath.'
    }
    if ($StretchSourcePath) {
        $stretchSource = (Resolve-Path -LiteralPath $StretchSourcePath).Path
        $arguments += $stretchSource
    }
    if ($IntermediateSurfacePath) {
        $intermediateSurface = (Resolve-Path -LiteralPath $IntermediateSurfacePath).Path
        $arguments += $intermediateSurface
    }
    if ($DestinationBeforePath) {
        $destinationBefore = (Resolve-Path -LiteralPath $DestinationBeforePath).Path
        $clipRectangles = (Resolve-Path -LiteralPath $ClipRectanglesPath).Path
        $arguments += @($destinationBefore, $clipRectangles)
    }
    if ($StretchParametersPath) {
        $arguments += (Resolve-Path -LiteralPath $StretchParametersPath).Path
    }
    & $Executable @arguments
}
if ($LASTEXITCODE -ne 0) {
    throw "Original SGL comparison failed with exit code $LASTEXITCODE"
}
