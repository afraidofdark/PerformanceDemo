# Streamline on the SM-G950F (Mali-G71 MP20)

Arm Performance Studio 2024.3 is installed on the workstation:
`C:\Program Files\Arm\Arm Performance Studio 2024.3` - Streamline 9.2.2 (GUI + CLI), the Mali
Offline Compiler 8.4.1 used for `../malioc`, Frame Advisor, RenderDoc for Arm GPUs.

This folder holds the first GPU captures taken with it, and the recipe that produced them.

## What it took to get a capture on this device

Two blockers, both in the way the tool assumes a phone behaves:

1. **gatord as root does not start under Magisk.** `su -c /data/local/tmp/gatord` dies with
   `lib::popen(getprop security.perf_harden) failed: errno = 13` and then
   `Unable to set security.perf_harden property`. The popen call it uses to read and write the
   perf properties is refused in the `u:r:magisk:s0` domain, so the daemon never gets past its
   own property handshake. Running it as the shell user works.
2. **A non-root capture needs a debuggable package.** gatord starts its agent with `run-as
   <package>`, which fails on a normal release APK:
   `run-as: package not debuggable: com.otyazilim.toolkit.insomnia`.

So the capture build is the release build with `debuggable true` added to the `release` build type
in `Android/app/build.gradle`. The native code stays Release; only the manifest flag changes. Note
that the Packer regenerates that file from the engine template on every publish, so the flag has to
be re-added (or moved into the template) after a publish.

Two one-time device settings that a non-root capture also needs:

```
adb shell su -c 'setprop security.perf_harden 0'                              # perf firewall off
adb shell su -c 'echo -1 > /proc/sys/kernel/perf_event_paranoid'              # system wide perf
adb shell su -c 'setprop debug.perf_event_mlock_kb 8196'
```

`perf_event_paranoid` goes back to 1 on the next boot.

Mali counters themselves need **no root**: `/dev/mali0` is `crw-rw-rw-`, and gatord's userspace
Mali driver enumerates the counters as the shell user. `gatord --print counters.xml -S no
--android-pkg <pkg>` lists 362 of them on this device, including 133 usable `ARM_Mali-G71_*`
counters and the `ARMv8_Cortex_A53_*` / `ARMv8_Exynos_M1_*` CPU sets.

## Recipe

```powershell
$aps  = "C:\Program Files\Arm\Arm Performance Studio 2024.3"
$pkg  = "com.otyazilim.toolkit.insomnia"
$c    = "ARM_Mali-G71_GPU_ACTIVE,ARM_Mali-G71_FRAG_ACTIVE,..."   # comma separated, no spaces

adb push "$aps\streamline\bin\android\arm64\gatord" /data/local/tmp/gatord
adb shell "chmod 755 /data/local/tmp/gatord"

adb shell "am force-stop $pkg"
adb shell "nohup /data/local/tmp/gatord -S no --android-pkg $pkg -C $c -o /data/local/tmp/cap.apc -t 12 > /data/local/tmp/gatord.out 2>&1 &"
adb shell "am start -n $pkg/com.otyazilim.toolkit.ToolKitActivity"
# ... wait for the capture to end, then:
adb pull /data/local/tmp/cap.apc .

# numbers out of the capture:
& "$aps\streamline\Streamline-cli.exe" -report .\cap.apc -timeline -output .\report
```

`gatord` writes an `.apc` directory (`0000000000` series + `captured.xml` + `counters.xml` +
`events.xml`), which the GUI opens directly and `-report` turns into `timeline.csv` (one row per
~1 ms sample, one column per counter). The GUI is at
`C:\Program Files\Arm\Arm Performance Studio 2024.3\streamline\Streamline-gui.exe`.

`templates/` holds the stock `.st` chart templates extracted from
`plugins/com.arm.streamline_9.2.2...jar` (`Mali-G71 - Bifrost.st` is the one that matches this GPU);
`mali-g71.st` is a copy with a quote-free name for command lines.

## Captures

| folder | counters | what it answers |
| --- | --- | --- |
| `cap4.apc` | default set (CPU only) | first proof the pipeline works: A53 counters, no Mali perf counters |
| `cap5.apc` | 12 Mali + A53 cycles | one broad GPU/bandwidth/tile set |
| `cap6.apc` | clock + GPU/fragment/tiler + ALU + tex + external beats | utilization against the clock, and DRAM traffic |
| `cap7.apc` | fragment + stall reasons | what the shader cores are waiting on |

Each has a `report*/timeline.csv` next to it.

### Numbers (steady state, from 4 s into the 12 s capture)

From `cap6`:

```
gpu clock (average)    425 MHz
gpu active             375 Mcycles/s        -> 88 % of the clock
tiler active           36.9 % of gpu cycles -> geometry is not the limit
fragment units active  90.7 % per core     -> the fragment stage is
alu instructions       4.32 per gpu cycle  -> 1.6 G warp instr/s, 22 % of issue capacity
texture requests       2.02 per gpu cycle
external read beats    0.69 per gpu cycle
external write beats   0.45 per gpu cycle
```

From `cap7` (per shader core, share of gpu-active cycles):

```
fragment active          90.4 %
execution engine starved 69.7 %     <- cycles with no instruction ready
texture line-fill stall  21.5 %     <- texture cache line fills
texture partial data      3.0 %
texture coordinate        1.4 %
load/store unit read      0.5 %
load/store unit write     1.1 %
```

### Reading

The GPU is 88 % busy and the fragment stage owns the frame; the tiler is idle two thirds of the
time. Inside the fragment stage the execution engine is starved 70 % of its active cycles while ALU
issue runs at a fifth of capacity, the load/store unit is idle, and the largest named stall is
texture cache line fill. Nothing is throughput-saturated: not ALU issue, not the load/store unit,
not the external bus (0.69 + 0.45 beats per cycle).

That is a latency profile, not an arithmetic or a bandwidth one. It agrees with what the feature
toggles said in `../stage1-bottleneck-report.md` (pixel count dominates, colour format does not) and
sharpens it: the per-pixel cost is the dependent fetch chain of the forward shader, mostly texture
cache misses, so mip/aniso selection, distinct textures per pixel and UV locality are the levers
before instruction count is.

## Resolution scaling, and the wall behind it

`res99.apc`, `res65.apc` and `res35.apc` are the same counter set (clock, GPU active, fragment
active, execution starvation, texture line fill, texture requests) captured at three render
resolutions, one capture each, `debug.tk.res` set before the run. Same scene, same counters:

| `debug.tk.res` | render target | GPU util | fragment active | starved | texture line fill | tex/cycle | clock | app |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 99 % | 1069x2198 | 84 % | 94 % | 69 % | 12 % | 3.79 | 457 MHz | 15.0 fps |
| 65 % (default) | 702x1443 | 70 % | 91 % | 70 % | 10 % | 4.44 | 474 MHz | 22.5 fps |
| 35 % | 378x777 | 68 % | 74 % | 62 % | 9 % | 4.32 | 344 MHz | 34.4 fps |

Two things come out of it.

**The stall profile is per pixel, not per frame.** Pixels drop 8x from the top row to the bottom one
(2.35 Mpix to 0.29 Mpix) while execution starvation stays at 62-70 % and texture line fill only
slides from 12 % to 9 %. The shader cores are waiting the same fraction of their cycles no matter
how many pixels there are, so the fix is what a pixel fetches, not how many pixels are drawn.
Resolution scaling buys frame rate by drawing the same stall fewer times.

**At the project default the GPU is no longer the only limit.** 65 % puts the GPU at 70 % of a
474 MHz clock while the frame still takes 44 ms, and the app's own probe puts 43 ms of that in the
render path on the CPU. The capture's perf samples agree about where it goes:

```
<unknown code in libc.so>           42.6 % self samples
<unknown code in libGLES_mali.so>   25.0 % self
<unknown code in base.apk>          22.1 % self   (the game's own libmain.so)
<unknown code in libart.so>          5.3 % self
```

Half the CPU samples are inside libc and the GL driver, which for this engine means buffer and
uniform uploads plus draw submission - not scene traversal, culling or the profiler. The functions
are unsymbolised because a local capture does not retrieve the device images; symbolising them needs
the libraries pulled off the device (`/system/lib64/libc.so`, `libGLES_mali.so`, and the app's
`libmain.so`) and pointed at from Streamline.

`report-res65-fn` is that profile; `fn-logon` / `fn-logoff` are the same capture with the app's own
logging on and off, and they answer the question the probe raised about itself: libc goes 42.27 % to
41.31 %, so the `PERF`/`PROF` lines are not what the CPU is spending its time in.

### Who calls libc

`callpath/callpath.csv` is the call path table of the 65 % capture, and the unknown frames are still
attributable by *caller* - each libc row's nearest non-libc parent says which component made the
call:

| caller of a libc frame | samples | share |
| --- | --- | --- |
| Mali driver (`libGLES_mali.so`) | 1,921 | 53 % |
| `mali-cmar-backend` thread (same driver) | 311 | 9 % |
| the render thread itself, no frame in between | 271 | 7 % |
| `AudioTrack` / `libaudioclient` | 423 | 12 % |
| **the game's own `libmain.so`** | **245** | **7 %** |
| Android display / binder plumbing | 150 | 4 % |
| ART, JIT, sensors, misc | ~230 | 8 % |

So the memcpy-class time is the driver copying what the engine hands it - roughly two thirds of it -
and the game's own copies are 7 %. Interposing `memcpy` in the game's library would therefore see
only that 7 %; the lever is what is handed to the driver (draw count, uniform buffer size and update
rate), not the engine's own `memcpy` calls. The audio share is a reminder that the capture covers the
whole process, not just the frame.

### Memory

`report-mem/timeline.csv` is a capture with the process memory counters; the values are empty on a
non-root capture because `/proc` is mounted `hidepid=2` and gatord cannot read another process's
`statm`. Two other sources do work, and `Codes/PerfProbe.h` now reports the second one in the PERF
line as `rss=` and `faults=minor/major`:

- `dumpsys meminfo`, mid-run: PSS 143.7 MB total, of which native heap 65.2 MB, **EGL mtrack 55.9
  MB**, `.so` 6.2 MB, `.art` 6.7 MB.
- `/proc/self/statm` and `/proc/self/stat`, over a 25 s run at full resolution: RSS 450 -> 444 MB
  (min 443, max 452), VRAM 152 MB flat, and 574 minor faults in total - about 23 per second, 1.6 per
  frame.

Memory is therefore not part of the frame budget: no growth, no allocation churn, no swapping. The
largest single block is the driver's own EGL allocation, which tracks the render target.

### Caveat on the knob

`debug.tk.res` in `Codes/PerfProbe.h` used to apply only when the value was not 100, so it could not
override a project default that is not 1.0 - the 99 % row above is how the full resolution case was
measured after `res=100` silently kept the 65 % default. Fixed: the knob reads -1 as "not set" now
and any positive value, 100 included, is applied (verified: `res=100` gives `res=1080x2220`).

### Anisotropic filtering is not available here

The engine's extension probes used `glGetString(GL_EXTENSIONS)`, which answers null on a GL ES 3.2
context, so all three extension flags read as unsupported. That is fixed (the enumeration path is
used now, and the count is logged). With the fix the answer for this phone is still "no": the
Mali-G71 r0p0 driver enumerates 96 extensions and `EXT_texture_filter_anisotropic`,
`OES_texture_float_linear` and `EXT_depth_clamp` are not among them. Anisotropic filtering has
therefore never done anything on this device, and it cannot be used as the texture-locality
experiment; a mip bias would need sampler objects, because GL ES has no texture level LOD bias.


