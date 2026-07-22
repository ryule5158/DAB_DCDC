param(
    [Parameter(Mandatory = $true)][string]$InputDocx,
    [Parameter(Mandatory = $true)][string]$OutputPdf
)

$ErrorActionPreference = 'Stop'
$docx = (Resolve-Path -LiteralPath $InputDocx).Path
$pdf = [System.IO.Path]::GetFullPath($OutputPdf)
$word = $null
$document = $null
try {
    Write-Output 'WORD_CREATE'
    $word = New-Object -ComObject Word.Application
    $word.Visible = $false
    $word.DisplayAlerts = 0
    $word.ScreenUpdating = $false
    Write-Output 'WORD_OPEN'
    $document = $word.Documents.Open($docx, $false, $true, $false)
    Write-Output ('WORD_OPENED pages=' + $document.ComputeStatistics(2))
    $document.ExportAsFixedFormat($pdf, 17, $false, 0, 0, 1, 9999, 0, $true, $true, 1, $true, $true, $false)
    Write-Output 'PDF_EXPORTED'
}
finally {
    if ($null -ne $document) {
        $document.Close($false)
        [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($document)
    }
    if ($null -ne $word) {
        $word.Quit()
        [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($word)
    }
    [GC]::Collect()
    [GC]::WaitForPendingFinalizers()
}
