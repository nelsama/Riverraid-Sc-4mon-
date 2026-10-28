param([string]$out = "docs/img/seq/desktop.png")

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32 {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int ht, bool repaint);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
"@
$p = Get-Process emulador -ErrorAction Stop | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
$h = $p.MainWindowHandle
$scr = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
# Recoloca la ventana para que quepa entera en pantalla (deja 40px para la barra).
$w = [Math]::Min(960, $scr.Width - 40)
$ht = [Math]::Min(720 + 40, $scr.Height - 80)
[Win32]::ShowWindow($h, 9) | Out-Null
[Win32]::MoveWindow($h, 10, 10, $w, $ht, $true) | Out-Null
[Win32]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 800
$r = New-Object Win32+RECT
[Win32]::GetWindowRect($h, [ref]$r) | Out-Null
$cw = $r.R - $r.L
$ch = $r.B - $r.T
$bmp = New-Object System.Drawing.Bitmap $cw, $ch
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L, $r.T, 0, 0, (New-Object System.Drawing.Size $cw, $ch))
$bmp.Save($out)
$g.Dispose()
$bmp.Dispose()
Write-Output "$($r.L),$($r.T) ${cw}x${ch}"
