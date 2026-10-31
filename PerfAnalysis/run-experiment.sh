#!/usr/bin/env bash
#
# One measurement run of the PerformanceDemo app on the attached device.
#
# Linux counterpart of run-experiment.ps1, same method: the app is force-stopped before and after
# the run, every knob is reset to -1 (the neutral value), the frame period is sampled from
# SurfaceFlinger and the process CPU from /proc, and the PERF/PROF lines are pulled out of logcat.
# A phone left rendering for minutes heats up and throttles, so runs stay short and cold.
#
# Usage:
#   ./run-experiment.sh base
#   ./run-experiment.sh res50 --set res=50
#   ./run-experiment.sh noaa  --set msaa=0,hdr=0,ssao=0
#   ./run-experiment.sh small --window 720x1480
#   ./run-experiment.sh quick --warmup 10 --seconds 12
#
# Differences from the PowerShell version:
#   - cpu= is a real percentage of one core. The PowerShell line divided clock ticks by the run
#     length and printed that as %, which is a factor of the kernel's CLK_TCK away from a percentage
#     (100 on this device). This script reads CLK_TCK and reports (ticks / CLK_TCK) / seconds * 100.
#   - The package is a variable, because the project folder (and with it the app id) changes when a
#     project is renamed.
#
set -uo pipefail

pkg="com.otyazilim.toolkit.PerformanceDemo"
act="$pkg/com.otyazilim.toolkit.ToolKitActivity"
out_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
knobs=(res msaa hdr ssao bloom tm cull batch shadow mesh perf prof)

label=""
set_args=""
warmup=14
seconds=20
window=""

while [ $# -gt 0 ]; do
  case "$1" in
    --set) set_args="${2:-}"; shift 2 ;;
    --warmup) warmup="${2:-14}"; shift 2 ;;
    --seconds) seconds="${2:-20}"; shift 2 ;;
    --window) window="${2:-}"; shift 2 ;;
    --package) pkg="${2:-$pkg}"; act="$pkg/com.otyazilim.toolkit.ToolKitActivity"; shift 2 ;;
    -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
    *) label="$1"; shift ;;
  esac
done

if [ -z "$label" ]; then
  echo "usage: $0 <label> [--set a=1,b=2] [--warmup N] [--seconds N] [--window WxH]" >&2
  exit 2
fi

adb_bin="${ANDROID_HOME:-$HOME/Android/Sdk}/platform-tools/adb"
if [ -x "$adb_bin" ]; then
  adb() { "$adb_bin" "$@"; }
fi

get_pid() {
  local p
  p="$(adb shell pidof "$pkg" 2>/dev/null | tr -d '\r\n ')"
  [ -n "$p" ] && echo "$p" || return 1
}

stop_app() {
  adb shell am force-stop "$pkg" >/dev/null 2>&1
  local i pid
  for i in $(seq 1 10); do
    sleep 0.4
    get_pid >/dev/null 2>&1 || return 0
  done

  # force-stop occasionally leaves the process behind; SIGKILL it by pid and look again.
  pid="$(get_pid 2>/dev/null)"
  if [ -n "$pid" ]; then
    adb shell "kill -9 $pid" >/dev/null 2>&1
    sleep 1
    get_pid >/dev/null 2>&1 || return 0
  fi

  return 1
}

# A scene left running heats the phone, and every later measurement is then taken on a hotter
# machine than the baseline. The app is therefore closed - and the close verified - before the run,
# after the run, and on every exit path, including a failing one.
close_app() {
  if stop_app; then
    printf '  app closed: yes (%s)\n' "$pkg"
  else
    printf '  app closed: NO - still running, kill it by hand (%s)\n' "$pkg" >&2
  fi

  # Clear the knobs on the way out as well. They live in the persistent property store, so a knob set
  # by a hand run outlives the app, and the next launch - by anyone, including the person holding the
  # phone - starts with it applied. A camera oscillation knob left set looks exactly like a rendering
  # bug on the device.
  for k in "${knobs[@]}"; do
    adb shell "setprop debug.tk.$k -1" >/dev/null 2>&1
  done
}

trap close_app EXIT INT TERM

surface_layer() {
  adb shell dumpsys SurfaceFlinger --list 2>/dev/null |
    tr -d '\r\000' |
    grep -m 1 "^SurfaceView - $(printf '%s' "$pkg" | sed 's/\./\\./g')" |
    sed 's/[[:space:]]*$//'
}

# Mean frame period from the surface's present timestamps, in ms. Columns are
# desired / actual / fence; only `actual` is used, and only rows the compositor filled in.
frame_stats() {
  local layer raw
  layer="$(surface_layer)"
  [ -z "$layer" ] && return 1
  raw="$(adb shell dumpsys SurfaceFlinger --latency "'$layer'" 2>/dev/null | tr -d '\r\000')"
  printf '%s\n' "$raw" |
    awk '
      NF == 3 && $2 ~ /^[0-9]+$/ && $2 > 0 && $2 < 9000000000000000000 { print $2 }
    ' |
    sort -n |
    awk '
      NR == 1 { prev = $1; next }
      {
        d = ($1 - prev) / 1000000.0;
        prev = $1;
        if (d > 0.5) { sum += d; n++ }
      }
      END {
        if (n < 5) { exit 1 }
        printf "%.3f %d\n", sum / n, n
      }
    '
}

cpu_ticks() {
  adb shell "cat /proc/$1/stat" 2>/dev/null | tr -d '\r' |
    awk '{ print $14 + $15 }'
}

# USER_HZ. getconf is missing on this device's toybox, so the default holds the measured value:
# /proc/stat advances ~100 jiffies per second per online core here (overflow at 8 cores x 100).
clk_tck="${CLK_TCK_OVERRIDE:-100}"
if cmt="$(adb shell getconf CLK_TCK 2>/dev/null | tr -d '\r\n ')" && [ -n "$cmt" ]; then
  clk_tck="$cmt"
fi

# ---- device closed, then configured ---------------------------------------------------------------
# Never measure on top of a scene that is already running: the device would start the run warm. The
# check is reported, not assumed, so a leftover process shows up in the output instead of in the next
# run's numbers.
if ! stop_app; then
  echo "error: $pkg would not stop before the run" >&2
  exit 1
fi
printf 'app closed before the run: yes (%s)\n' "$pkg"

if [ -n "$window" ]; then
  adb shell wm size "$window" >/dev/null
  sleep 3
fi

# Reset to -1 rather than an empty value: the property service refuses `setprop x ''` on some Android
# versions and a stale knob from the previous run would silently change this one.
for k in "${knobs[@]}"; do
  adb shell "setprop debug.tk.$k -1" >/dev/null 2>&1
done

# Entries may be comma or space separated.
for kv in $(printf '%s' "$set_args" | tr ',' ' '); do
  [ -z "$kv" ] && continue
  adb shell "setprop debug.tk.${kv%%=*} '${kv#*=}'" >/dev/null 2>&1
done

adb logcat -c >/dev/null 2>&1
adb shell am start -n "$act" >/dev/null 2>&1

# Scene load plus shader warmup. The probe skips its own first frames as well.
sleep "$warmup"

pid="$(get_pid)"
if [ -z "$pid" ]; then
  echo "error: app did not start" >&2
  exit 1
fi

t0="$(cpu_ticks "$pid")"
f1="$(frame_stats)"
sleep "$seconds"
f2="$(frame_stats)"
t1="$(cpu_ticks "$pid")"

# Close the app before parsing anything: the phone stops rendering now, and the log is still in the
# buffer. The exit trap closes again and reports whether the close actually took.
stop_app >/dev/null 2>&1

log="$out_dir/$label.log"
adb logcat -d -s TK_LOG:V 2>/dev/null | tr -d '\r' |
  grep -E "PERF|PROF|GPU timer|Shader in compile" >"$log" || true

perf="$(grep "PERF fps" "$log" | tail -3)"
cfg="$(grep "PERF config" "$log" | tail -1)"
timer="$(grep "GPU timer" "$log" | tail -1)"

win="$(adb shell wm size 2>/dev/null | tr -d '\r' | sed -n 's/.*Override size: *\([0-9]*x[0-9]*\).*/\1/p' | head -1)"
[ -z "$win" ] && win="?"

stats="${f2:-$f1}"
cpu_pct="$(awk -v dt="$((t1 - t0))" -v hz="$clk_tck" -v sec="$seconds" \
  'BEGIN { printf "%.1f", (dt / hz) / sec * 100.0 }')"

if [ -n "$stats" ]; then
  read -r period frames <<<"$stats"
  fps="$(awk -v p="$period" 'BEGIN { printf "%.2f", 1000.0 / p }')"
  printf '%-18s win=%-10s sffps=%6s frame=%6sms cpu=%5s%% frames=%s\n' \
    "$label" "$win" "$fps" "$period" "$cpu_pct" "$frames"
else
  printf '%-18s win=%-10s sffps=   n/a frame=   n/a cpu=%5s%% (no SurfaceFlinger samples)\n' \
    "$label" "$win" "$cpu_pct"
fi
strip() { sed 's/^.*TK_LOG *: *//'; }
[ -n "$timer" ] && printf '  %s\n' "$(printf '%s' "$timer" | strip)"
[ -n "$cfg" ] && printf '  %s\n' "$(printf '%s' "$cfg" | strip)"
printf '%s\n' "$perf" | while IFS= read -r line; do
  [ -n "$line" ] && printf '  %s\n' "$(printf '%s' "$line" | strip)"
done
printf '  log: %s\n' "$log"

{
  printf '%-18s win=%-10s sffps=%6s frame=%6sms cpu=%5s%%\n' \
    "$label" "$win" "${fps:-n/a}" "${period:-n/a}" "$cpu_pct"
} >>"$out_dir/results.txt"
printf '%s\n' "$perf" | strip >>"$out_dir/results.txt"
