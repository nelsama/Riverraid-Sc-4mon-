#!/usr/bin/env python3
"""port.py - Encuentra los puertos TCP en escucha del emulador (UART, debug).
Uso: python tools/port.py
"""
import subprocess

out = subprocess.run(
    ["powershell", "-Command",
     "Get-NetTCPConnection -State Listen | "
     "Where-Object { $_.LocalPort -gt 1024 -and $_.LocalPort -lt 65535 } | "
     "Select-Object LocalAddress,LocalPort,OwningProcess | Format-Table -AutoSize | Out-String -Width 120"],
    capture_output=True, text=True)
print(out.stdout)
print(out.stderr)
