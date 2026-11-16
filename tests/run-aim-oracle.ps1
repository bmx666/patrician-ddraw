param(
    [Parameter(Mandatory)] [string]$ReferenceDirectory,
    [Parameter(Mandatory)] [string]$EncodedImage,
    [Parameter(Mandatory)] [string]$ReferencePixels,
    [string]$ReferencePalette,
    [switch]$Cursor,
    [string]$Executable = (Join-Path $PSScriptRoot '..\build\Release\original_aim_oracle.exe')
)

$ErrorActionPreference = 'Stop'
$reference = (Resolve-Path -LiteralPath $ReferenceDirectory).Path
$imageHash = '6E228E2E4F60DD1D18C6F9AF1BAA47F651D9BF32AB1F5A3381B26499E0C32F55'
$pixelHash = '7CE7AAA8CB96196B83EAC535734D687C1174DBB5A3F96FBA1B6656C53D56D713'
if ($Cursor -and $ReferencePalette) { throw 'The cursor fixture has no palette.' }
if ($Cursor) {
    $imageHash = '1F9204E37921D679A39BC54E092DC5D9D174540B1753E4C9A48C681EB3AE28AB'
    $pixelHash = '44D113EA74574BFD8BE6987CF258800EB0F6C95F51A1A3940E46996FC447EF79'
}
if ($ReferencePalette) {
    $imageHash = 'E7B46BDCA2F51F213F8E5EFE6ED8AEBB345C11C4F41DF6DD52D16BBD23CE287E'
    $pixelHash = '9D76E354CE5B8955E22140DFF9D6CD3F7AD015C1E01A631C3B90B336D8602896'
}
$files = @(
    @{ Path = (Join-Path $reference 'AIM.dll'); Hash = '4C75CB3E6EE485DE8B7ED30CD5DFABDAA7EA44BAC72B523A01C60B7205D71C72' }
    @{ Path = (Join-Path $reference 'ijl11.dll'); Hash = '5A0B1C4E5D91FD67A1AD23E5CE869899B79A7282CB6E5533DC5C074EB59306EC' }
    @{ Path = $EncodedImage; Hash = $imageHash }
    @{ Path = $ReferencePixels; Hash = $pixelHash }
)
if ($ReferencePalette) {
    $files += @{ Path = $ReferencePalette; Hash = 'B3D49F251D383669166774FD78234AFBBE91572EFDDE3B7383BCDCDF96D44CB5' }
}
foreach ($file in $files) {
    if ((Get-FileHash -LiteralPath $file.Path -Algorithm SHA256).Hash -ne $file.Hash) {
        throw "Unexpected reference file: $($file.Path)"
    }
}

$arguments = @($reference, $EncodedImage, $ReferencePixels)
if ($ReferencePalette) { $arguments += $ReferencePalette }
& $Executable @arguments
if ($LASTEXITCODE -ne 0) {
    throw "AIM comparison failed with exit code $LASTEXITCODE"
}
