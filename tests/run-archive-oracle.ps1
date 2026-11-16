param(
    [Parameter(Mandatory)] [string]$ReferenceDirectory,
    [Parameter(Mandatory)] [string]$ExpectedImage,
    [string]$ReferencePixels,
    [string]$Executable = (Join-Path $PSScriptRoot '..\build\Release\original_archive_oracle.exe')
)

$ErrorActionPreference = 'Stop'
$reference = (Resolve-Path -LiteralPath $ReferenceDirectory).Path
$files = @(
    @{ Path = (Join-Path $reference 'archiver.dll'); Hash = '044E6D659F41B86A00C5429F66F127B7BD01012F39DFF008FAF4EBD6887BD114' }
    @{ Path = (Join-Path $reference 'p2arch0_eng.cpr'); Hash = '6EBD9AA5A42C496316DD2D137EA99727D200CA557808C15D7EC1B473D4079009' }
    @{ Path = $ExpectedImage; Hash = '6E228E2E4F60DD1D18C6F9AF1BAA47F651D9BF32AB1F5A3381B26499E0C32F55' }
)
if ($ReferencePixels) {
    $files += @{ Path = (Join-Path $reference 'AIM.dll'); Hash = '4C75CB3E6EE485DE8B7ED30CD5DFABDAA7EA44BAC72B523A01C60B7205D71C72' }
    $files += @{ Path = (Join-Path $reference 'ijl11.dll'); Hash = '5A0B1C4E5D91FD67A1AD23E5CE869899B79A7282CB6E5533DC5C074EB59306EC' }
    $files += @{ Path = $ReferencePixels; Hash = '7CE7AAA8CB96196B83EAC535734D687C1174DBB5A3F96FBA1B6656C53D56D713' }
}
foreach ($file in $files) {
    if ((Get-FileHash -LiteralPath $file.Path -Algorithm SHA256).Hash -ne $file.Hash) {
        throw "Unexpected reference file: $($file.Path)"
    }
}

$arguments = @($reference, $ExpectedImage)
if ($ReferencePixels) { $arguments += $ReferencePixels }
& $Executable @arguments
if ($LASTEXITCODE -ne 0) {
    throw "Archive comparison failed with exit code $LASTEXITCODE"
}
