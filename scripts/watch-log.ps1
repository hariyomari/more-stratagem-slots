<#
.SYNOPSIS
    Follows this mod's log in the newest HellForge session, live, and switches to the next session when one starts.

.DESCRIPTION
    HellForge writes logs\play-session.json naming the newest session's folder; the mod's log in it is
    mod-<id>.log, with <id> read from the mod's hellforge-mod.json. This prints that log from the start, then every
    line as it is written, until Ctrl+C.

    By default it waits for a session that starts after the script does, so an old session's log is not shown.
    -Current follows the newest session even if it started earlier.

.PARAMETER HellForge
    The HellForge installation (the folder with app\ and mods\). Defaults to the HELLFORGE environment variable.

.PARAMETER Current
    Also follow the newest existing session, instead of waiting for a new one.

.EXAMPLE
    .\scripts\watch-log.ps1
    .\scripts\watch-log.ps1 -Current
    .\scripts\watch-log.ps1 -HellForge B:\HellForge
#>
param(
    [string] $HellForge = $env:HELLFORGE,
    [switch] $Current
)

$ErrorActionPreference = 'Stop'

if (-not $HellForge) {
    Write-Error 'Give your HellForge folder: watch-log.ps1 -HellForge B:\HellForge (or set the HELLFORGE environment variable)'
}
$pointer = Join-Path $HellForge 'logs\play-session.json'

# The mod's id, from the manifest in the mod folder next to scripts\.
$root = Split-Path $PSScriptRoot -Parent
$manifest = Get-ChildItem $root -Filter 'hellforge-mod.json' -Recurse -Depth 1 |
    Where-Object { $_.FullName -notmatch '\\(build|\.idea|\.git)\\' } |
    Select-Object -First 1
if (-not $manifest) {
    Write-Error "No hellforge-mod.json in a folder under $root"
}
# Read as UTF-8 so a byte order mark at the start does not break the JSON.
$modId = (Get-Content $manifest.FullName -Raw -Encoding UTF8 | ConvertFrom-Json).id
$logName = "mod-$modId.log"

function Get-Session {
    if (-not (Test-Path $pointer)) { return $null }
    try {
        # HellForge may be rewriting the file; a failed read is tried again on the next poll.
        $json = Get-Content $pointer -Raw -ErrorAction Stop | ConvertFrom-Json
        return [pscustomobject]@{ Id = $json.runId; Folder = $json.run }
    } catch {
        return $null
    }
}

$session = Get-Session
$skipId = if ($Current -or -not $session) { $null } else { $session.Id }

Write-Host "Watching $logName under $HellForge\logs\sessions (Ctrl+C to stop)" -ForegroundColor DarkGray
if ($skipId) {
    Write-Host 'Waiting for a new session. Use -Current to follow the newest existing one.' -ForegroundColor DarkGray
}

$reader = $null
$followingId = $null
$waitingShown = $false

try {
    while ($true) {
        $session = Get-Session

        # A new session: drop the old log and look for this one's.
        if ($session -and $session.Id -ne $skipId -and $session.Id -ne $followingId) {
            if ($reader) { $reader.Dispose(); $reader = $null }
            $followingId = $session.Id
            $waitingShown = $false
            Write-Host ''
            Write-Host "== session $($session.Id)" -ForegroundColor Cyan
        }

        # Open the log once the session has written it. FileShare.ReadWrite lets HellForge keep writing to it.
        if ($followingId -and -not $reader) {
            $path = Join-Path $session.Folder $logName
            if (Test-Path $path) {
                $stream = [System.IO.File]::Open($path, 'Open', 'Read', 'ReadWrite, Delete')
                $reader = New-Object System.IO.StreamReader($stream, [System.Text.Encoding]::UTF8)
            } elseif (-not $waitingShown) {
                Write-Host "Waiting for $logName (the mod has not loaded yet, or it did not build: see session.log and cli-output.txt)" -ForegroundColor DarkGray
                $waitingShown = $true
            }
        }

        # Print whatever is new.
        if ($reader) {
            while ($null -ne ($line = $reader.ReadLine())) {
                Write-Host $line
            }
        }

        Start-Sleep -Milliseconds 250
    }
} finally {
    if ($reader) { $reader.Dispose() }
}
