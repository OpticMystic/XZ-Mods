import { spawnSync } from 'node:child_process';

// On Windows Edge relaunches itself at start, so the spawned process is only a launcher and child.kill() leaves
// the real browser running the test page (and its timers), filling %TEMP%. The unique profile folder names every
// process of this browser, so end those.
export function killBrowser(child, profile) {
  child?.kill();
  if (process.platform !== 'win32' || !profile) return;
  const script = "Get-CimInstance Win32_Process -Filter \"Name='msedge.exe'\" | Where-Object { $_.CommandLine -and $_.CommandLine.Contains($env:XZ_PROFILE) } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }";
  spawnSync('powershell.exe', ['-NoProfile', '-NonInteractive', '-Command', script], { stdio: 'ignore', windowsHide: true, env: { ...process.env, XZ_PROFILE: profile } });
}
