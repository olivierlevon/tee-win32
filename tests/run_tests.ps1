# =============================================================================
# tee-win32 — Automated validation tests
# =============================================================================
# Usage:  powershell -ExecutionPolicy Bypass -File tests/run_tests.ps1 -TeeBinary <path>
# Exit code: 0 = all passed, 1 = one or more failures
# =============================================================================

param(
    [Parameter(Mandatory=$true)]
    [string]$TeeBinary
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

$script:passed = 0
$script:failed = 0
$script:testDir = Join-Path $env:TEMP "tee-win32-tests-$(Get-Random)"

function Setup-TestDir {
    if (Test-Path $script:testDir) { Remove-Item -Recurse -Force $script:testDir }
    New-Item -ItemType Directory -Path $script:testDir | Out-Null
}

function Cleanup-TestDir {
    if (Test-Path $script:testDir) { Remove-Item -Recurse -Force $script:testDir }
}

function Assert-True {
    param([string]$TestName, [bool]$Condition, [string]$Detail = "")
    if ($Condition) {
        Write-Host "  PASS  $TestName" -ForegroundColor Green
        $script:passed++
    } else {
        Write-Host "  FAIL  $TestName  $Detail" -ForegroundColor Red
        $script:failed++
    }
}

function Run-Tee {
    param(
        [string]$Input,
        [string[]]$Args,
        [switch]$ExpectFailure
    )
    $outFile = Join-Path $script:testDir "stdout.tmp"
    $errFile = Join-Path $script:testDir "stderr.tmp"

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $TeeBinary
    $psi.Arguments = ($Args -join " ")
    $psi.UseShellExecute = $false
    $psi.RedirectStandardInput = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true

    $proc = New-Object System.Diagnostics.Process
    $proc.StartInfo = $psi
    $proc.Start() | Out-Null

    # Read stderr asynchronously to avoid deadlocks
    $stderrTask = $proc.StandardError.ReadToEndAsync()

    if ($Input) {
        $proc.StandardInput.Write($Input)
    }
    $proc.StandardInput.Close()

    $stdout = $proc.StandardOutput.ReadToEnd()
    $proc.WaitForExit()
    $stderr = $stderrTask.GetAwaiter().GetResult()

    $code = $proc.ExitCode
    $proc.Dispose()

    return @{
        ExitCode = $code
        Stdout   = $stdout
        Stderr   = $stderr
    }
}

# ---------------------------------------------------------------------------
# Resolve binary path
# ---------------------------------------------------------------------------

$TeeBinary = (Resolve-Path $TeeBinary).Path
if (-not (Test-Path $TeeBinary)) {
    Write-Host "ERROR: Binary not found: $TeeBinary" -ForegroundColor Red
    exit 1
}
Write-Host "=== tee-win32 test suite ===" -ForegroundColor Cyan
Write-Host "Binary: $TeeBinary"
Write-Host ""

Setup-TestDir

try {

# ===========================================================================
# 1. --version
# ===========================================================================
Write-Host "[1] --version" -ForegroundColor Yellow
$r = Run-Tee -Input "" -Args @("--version", "NUL")
Assert-True "exit code is 0"         ($r.ExitCode -eq 0)
Assert-True "output contains version string" ($r.Stderr -match "tee for Windows v\d+\.\d+\.\d+")

# ===========================================================================
# 2. --help
# ===========================================================================
Write-Host "[2] --help" -ForegroundColor Yellow
$r = Run-Tee -Input "" -Args @("--help", "NUL")
Assert-True "exit code is 0"         ($r.ExitCode -eq 0)
Assert-True "output contains Usage"  ($r.Stderr -match "Usage:")
Assert-True "output lists options"   ($r.Stderr -match "--append")

# ===========================================================================
# 3. Basic pipe-through: stdin → stdout + file
# ===========================================================================
Write-Host "[3] Basic pipe-through" -ForegroundColor Yellow
$outPath = Join-Path $script:testDir "basic.txt"
$r = Run-Tee -Input "hello world`n" -Args @($outPath)
Assert-True "exit code is 0"               ($r.ExitCode -eq 0)
Assert-True "stdout matches input"          ($r.Stdout.TrimEnd() -eq "hello world")
Assert-True "file matches input"            ((Get-Content $outPath -Raw).TrimEnd() -eq "hello world")

# ===========================================================================
# 4. Append mode (-a)
# ===========================================================================
Write-Host "[4] Append mode (-a)" -ForegroundColor Yellow
$outPath = Join-Path $script:testDir "append.txt"
Set-Content -Path $outPath -Value "line1" -NoNewline
$r = Run-Tee -Input "line2`n" -Args @("-a", $outPath)
Assert-True "exit code is 0"               ($r.ExitCode -eq 0)
$content = (Get-Content $outPath -Raw)
Assert-True "file contains original + new"  ($content -match "line1" -and $content -match "line2")

# ===========================================================================
# 5. Multiple output files
# ===========================================================================
Write-Host "[5] Multiple output files" -ForegroundColor Yellow
$f1 = Join-Path $script:testDir "multi1.txt"
$f2 = Join-Path $script:testDir "multi2.txt"
$r = Run-Tee -Input "multi test`n" -Args @($f1, $f2)
Assert-True "exit code is 0"               ($r.ExitCode -eq 0)
Assert-True "stdout matches"                ($r.Stdout.TrimEnd() -eq "multi test")
Assert-True "file1 matches"                 ((Get-Content $f1 -Raw).TrimEnd() -eq "multi test")
Assert-True "file2 matches"                 ((Get-Content $f2 -Raw).TrimEnd() -eq "multi test")

# ===========================================================================
# 6. Strip ANSI codes (-s)
# ===========================================================================
Write-Host "[6] Strip ANSI codes (-s)" -ForegroundColor Yellow
$outPath = Join-Path $script:testDir "strip.txt"
# Send text with ANSI color escape: ESC[31m = red, ESC[0m = reset
$ansiInput = "$([char]27)[31mRED TEXT$([char]27)[0m normal`n"
$r = Run-Tee -Input $ansiInput -Args @("-s", $outPath)
Assert-True "exit code is 0"               ($r.ExitCode -eq 0)
$stripped = (Get-Content $outPath -Raw)
Assert-True "file has no ESC chars"         ($stripped -notmatch [char]27)
Assert-True "file contains text"            ($stripped -match "RED TEXT")
Assert-True "file contains normal"          ($stripped -match "normal")

# ===========================================================================
# 7. HTML conversion (--html)
# ===========================================================================
Write-Host "[7] HTML conversion (--html)" -ForegroundColor Yellow
$outPath = Join-Path $script:testDir "output.html"
$ansiInput = "$([char]27)[32mGREEN$([char]27)[0m`n"
$r = Run-Tee -Input $ansiInput -Args @("--html", $outPath)
Assert-True "exit code is 0"               ($r.ExitCode -eq 0)
$html = (Get-Content $outPath -Raw)
Assert-True "file contains <html> or <pre>" ($html -match "<html|<pre")
Assert-True "file contains GREEN text"      ($html -match "GREEN")

# ===========================================================================
# 8. Line numbers (-n)
# ===========================================================================
Write-Host "[8] Line numbers (-n)" -ForegroundColor Yellow
$outPath = Join-Path $script:testDir "linenum.txt"
$r = Run-Tee -Input "aaa`nbbb`nccc`n" -Args @("-n", "-s", $outPath)
Assert-True "exit code is 0"               ($r.ExitCode -eq 0)
$content = (Get-Content $outPath -Raw)
Assert-True "file contains line numbers"    ($content -match "1.*aaa" -and $content -match "2.*bbb")

# ===========================================================================
# 9. Timestamps (-t)
# ===========================================================================
Write-Host "[9] Timestamps (-t)" -ForegroundColor Yellow
$outPath = Join-Path $script:testDir "timestamp.txt"
$r = Run-Tee -Input "timestamped`n" -Args @("-t", "-s", $outPath)
Assert-True "exit code is 0"               ($r.ExitCode -eq 0)
$content = (Get-Content $outPath -Raw)
# ISO 8601 pattern: YYYY-MM-DDThh:mm:ss
Assert-True "file contains ISO timestamp"   ($content -match "\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}")

# ===========================================================================
# 10. Grep filtering (--grep)
# ===========================================================================
Write-Host "[10] Grep filtering (--grep)" -ForegroundColor Yellow
$outPath = Join-Path $script:testDir "grep.txt"
$input = "ERROR: something broke`nINFO: all good`nERROR: another issue`n"
$r = Run-Tee -Input $input -Args @("--grep", "ERROR", $outPath)
Assert-True "exit code is 0"               ($r.ExitCode -eq 0)
$content = (Get-Content $outPath -Raw)
Assert-True "file contains ERROR lines"     ($content -match "ERROR")
Assert-True "file does not contain INFO"    ($content -notmatch "INFO")
# stdout should still have ALL lines (grep applies to files only)
Assert-True "stdout has all lines"          ($r.Stdout -match "INFO")

# ===========================================================================
# 11. Combined options (-n -t -s)
# ===========================================================================
Write-Host "[11] Combined options (-nts)" -ForegroundColor Yellow
$outPath = Join-Path $script:testDir "combined.txt"
$ansiInput = "$([char]27)[31mhello$([char]27)[0m`n"
$r = Run-Tee -Input $ansiInput -Args @("-n", "-t", "-s", $outPath)
Assert-True "exit code is 0"               ($r.ExitCode -eq 0)
$content = (Get-Content $outPath -Raw)
Assert-True "has line number"               ($content -match "^\s*1")
Assert-True "has timestamp"                 ($content -match "\d{4}-\d{2}-\d{2}T")
Assert-True "ANSI stripped"                 ($content -notmatch [char]27)

# ===========================================================================
# 12. Invalid option
# ===========================================================================
Write-Host "[12] Invalid option" -ForegroundColor Yellow
$outPath = Join-Path $script:testDir "invalid.txt"
$r = Run-Tee -Input "" -Args @("--bogus", $outPath)
Assert-True "exit code is non-zero"         ($r.ExitCode -ne 0)
Assert-True "stderr mentions error"         ($r.Stderr -match "(?i)error|invalid")

# ===========================================================================
# 13. Mutually exclusive --html and --strip
# ===========================================================================
Write-Host "[13] Mutually exclusive --html --strip" -ForegroundColor Yellow
$outPath = Join-Path $script:testDir "exclusive.txt"
$r = Run-Tee -Input "" -Args @("--html", "--strip", $outPath)
Assert-True "exit code is non-zero"         ($r.ExitCode -ne 0)
Assert-True "stderr mentions exclusive"     ($r.Stderr -match "(?i)exclusive|mutually")

# ===========================================================================
# 14. Missing output file
# ===========================================================================
Write-Host "[14] Missing output file" -ForegroundColor Yellow
$r = Run-Tee -Input "data" -Args @()
Assert-True "exit code is non-zero"         ($r.ExitCode -ne 0)
Assert-True "stderr mentions missing file"  ($r.Stderr -match "(?i)missing|file")

# ===========================================================================
# 15. NUL device (data passthrough, no file)
# ===========================================================================
Write-Host "[15] NUL device passthrough" -ForegroundColor Yellow
$r = Run-Tee -Input "nul test`n" -Args @("NUL")
Assert-True "exit code is 0"               ($r.ExitCode -eq 0)
Assert-True "stdout matches"                ($r.Stdout.TrimEnd() -eq "nul test")

# ===========================================================================
# 16. Large data throughput
# ===========================================================================
Write-Host "[16] Large data throughput" -ForegroundColor Yellow
$outPath = Join-Path $script:testDir "large.txt"
$bigLine = ("X" * 1000) + "`n"
$bigInput = $bigLine * 100  # 100KB+
$r = Run-Tee -Input $bigInput -Args @($outPath)
Assert-True "exit code is 0"               ($r.ExitCode -eq 0)
$fileSize = (Get-Item $outPath).Length
Assert-True "file size > 90KB"              ($fileSize -gt 90000)

# ===========================================================================
# 17. --grep with invalid regex
# ===========================================================================
Write-Host "[17] --grep invalid regex" -ForegroundColor Yellow
$outPath = Join-Path $script:testDir "badregex.txt"
$r = Run-Tee -Input "data" -Args @("--grep", "[invalid", $outPath)
Assert-True "exit code is non-zero"         ($r.ExitCode -ne 0)
Assert-True "stderr mentions regex error"   ($r.Stderr -match "(?i)regex|pattern|error")

# ===========================================================================
# 18. --rotate without value
# ===========================================================================
Write-Host "[18] --rotate missing value" -ForegroundColor Yellow
$outPath = Join-Path $script:testDir "rotate.txt"
$r = Run-Tee -Input "data" -Args @("--rotate")
Assert-True "exit code is non-zero"         ($r.ExitCode -ne 0)

# ===========================================================================
# 19. --keep without --rotate warning
# ===========================================================================
Write-Host "[19] --keep without --rotate" -ForegroundColor Yellow
$outPath = Join-Path $script:testDir "keep.txt"
$r = Run-Tee -Input "data`n" -Args @("--keep", "3", $outPath)
Assert-True "exit code is 0 (just warns)"  ($r.ExitCode -eq 0)
Assert-True "stderr contains warning"       ($r.Stderr -match "(?i)warn|ignored")

# ===========================================================================
# 20. Flush mode (-f) does not break output
# ===========================================================================
Write-Host "[20] Flush mode (-f)" -ForegroundColor Yellow
$outPath = Join-Path $script:testDir "flush.txt"
$r = Run-Tee -Input "flush test`n" -Args @("-f", $outPath)
Assert-True "exit code is 0"               ($r.ExitCode -eq 0)
Assert-True "file matches"                  ((Get-Content $outPath -Raw).TrimEnd() -eq "flush test")

} finally {
    Cleanup-TestDir
}

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "=== Results ===" -ForegroundColor Cyan
Write-Host "  Passed: $script:passed" -ForegroundColor Green
Write-Host "  Failed: $script:failed" -ForegroundColor $(if ($script:failed -gt 0) { "Red" } else { "Green" })
Write-Host ""

if ($script:failed -gt 0) {
    Write-Host "VALIDATION FAILED" -ForegroundColor Red
    exit 1
} else {
    Write-Host "ALL TESTS PASSED" -ForegroundColor Green
    exit 0
}
