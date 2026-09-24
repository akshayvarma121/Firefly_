$p = Start-Process "E:\firefly\core\build\tests\Debug\test_pdlp.exe" -ArgumentList '--test', '"Explicit convergence check (1e-4 gap strict limits)"' -RedirectStandardError "E:\firefly\core\crash.err" -RedirectStandardOutput "E:\firefly\core\crash.out" -PassThru -NoNewWindow
Start-Sleep -Seconds 5
if (!$p.HasExited) { Stop-Process -Id $p.Id -Force }
Write-Output "STDERR:"
Get-Content E:\firefly\core\crash.err
Write-Output "STDOUT:"
Get-Content E:\firefly\core\crash.out
