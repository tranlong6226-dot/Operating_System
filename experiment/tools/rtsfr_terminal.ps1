param(
    [string]$Port,
    [ValidateRange(1200, 3000000)]
    [int]$BaudRate = 115200
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Select-RtsfrComPort {
    $ports = @([System.IO.Ports.SerialPort]::GetPortNames() |
        Sort-Object { [int](($_ -replace '[^0-9]', '0')) })

    if ($ports.Count -eq 0) {
        throw 'No COM port found. Check the USB-UART driver and reconnect the adapter.'
    }

    if ($ports.Count -eq 1) {
        Write-Host "Found one serial port: $($ports[0])"
        return $ports[0]
    }

    Write-Host 'Available COM ports:'
    for ($index = 0; $index -lt $ports.Count; $index++) {
        Write-Host ("  [{0}] {1}" -f ($index + 1), $ports[$index])
    }

    while ($true) {
        $answer = Read-Host 'Select port number'
        $selection = 0
        if ([int]::TryParse($answer, [ref]$selection) -and
            $selection -ge 1 -and $selection -le $ports.Count) {
            return $ports[$selection - 1]
        }
        Write-Host 'Invalid selection. Try again.' -ForegroundColor Yellow
    }
}

if ([string]::IsNullOrWhiteSpace($Port)) {
    $Port = Select-RtsfrComPort
}

$Port = $Port.ToUpperInvariant()
$availablePorts = @([System.IO.Ports.SerialPort]::GetPortNames())
if ($availablePorts -notcontains $Port) {
    throw "Port $Port is not available. Available: $($availablePorts -join ', ')"
}

$serial = [System.IO.Ports.SerialPort]::new(
    $Port,
    $BaudRate,
    [System.IO.Ports.Parity]::None,
    8,
    [System.IO.Ports.StopBits]::One
)
$serial.Handshake = [System.IO.Ports.Handshake]::None
$serial.NewLine = "`r`n"
$serial.ReadTimeout = 50
$serial.WriteTimeout = 500
$previousCtrlCMode = [Console]::TreatControlCAsInput
$line = [System.Text.StringBuilder]::new()

try {
    $serial.Open()
    [Console]::TreatControlCAsInput = $true

    Write-Host ''
    Write-Host "RTSFR terminal connected to $Port at $BaudRate baud." -ForegroundColor Green
    Write-Host 'Type commands and press Enter. Press Ctrl+C to exit.'
    Write-Host 'Quick commands: status, verify, demo d01 ... demo d05'
    Write-Host 'If no boot log appears, press the RESET button on the board.'
    Write-Host ''

    Start-Sleep -Milliseconds 100
    $serial.Write("format terminal`r`n")

    $running = $true
    while ($running) {
        if ($serial.BytesToRead -gt 0) {
            [Console]::Write($serial.ReadExisting())
        }

        while ([Console]::KeyAvailable) {
            $key = [Console]::ReadKey($true)
            $ctrlC = (($key.Modifiers -band [ConsoleModifiers]::Control) -ne 0) -and
                     ($key.Key -eq [ConsoleKey]::C)

            if ($ctrlC) {
                $running = $false
                break
            }

            if ($key.Key -eq [ConsoleKey]::Enter) {
                $command = $line.ToString().Trim()
                [Console]::WriteLine()
                if ($command.Length -gt 0) {
                    $serial.Write($command + "`r`n")
                }
                [void]$line.Clear()
            }
            elseif ($key.Key -eq [ConsoleKey]::Backspace) {
                if ($line.Length -gt 0) {
                    [void]$line.Remove($line.Length - 1, 1)
                    [Console]::Write("`b `b")
                }
            }
            elseif (-not [char]::IsControl($key.KeyChar)) {
                [void]$line.Append($key.KeyChar)
                [Console]::Write($key.KeyChar)
            }
        }

        Start-Sleep -Milliseconds 10
    }
}
catch [System.UnauthorizedAccessException] {
    Write-Host "Cannot open $Port. Close PuTTY, Serial Monitor, or another app using it." -ForegroundColor Red
    exit 2
}
catch {
    Write-Host "Serial error: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
finally {
    [Console]::TreatControlCAsInput = $previousCtrlCMode
    if ($serial.IsOpen) {
        $serial.Close()
    }
    $serial.Dispose()
    Write-Host "`nRTSFR terminal closed."
}
