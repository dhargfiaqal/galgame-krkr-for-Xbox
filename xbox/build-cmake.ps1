param([string] $GameArchiveUrl = '')
& (Join-Path $PSScriptRoot 'build.ps1') -Backend cmake -GameArchiveUrl $GameArchiveUrl