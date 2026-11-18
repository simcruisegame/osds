<#
  The AI models a release may ship: exactly these files, each cleared for shipping (CREDITS.md):
    data/ai/real-esrgan-general-x4v3.onnx  BSD 3-Clause (code and released weights)
    data/ai/scan-eye-student.onnx          MIT for our layers; the first two Satlas stages, ODC-BY 1.0
  Every other .onnx stays out: scripts\package.ps1 copies only these, and scripts\release.ps1
  fails on any other .onnx (or a Cityscapes file) in the zip. Dot-source this file.
#>
$script:ShippedModels = @(
    'data/ai/real-esrgan-general-x4v3.onnx',
    'data/ai/scan-eye-student.onnx'
)

function Get-UnshippableModels {
    # The zip entries (tar -t, "OSDS/..." or "OSDS\...") that are model weights not on the list.
    param([string[]]$Listing)
    $Allowed = @($script:ShippedModels | ForEach-Object { 'OSDS/' + $_ })
    @($Listing | ForEach-Object { $_ -replace '\\', '/' } | Where-Object {
        ($_ -match '(?i)\.onnx$' -and $Allowed -notcontains $_) -or $_ -match '(?i)cityscapes'
    })
}
