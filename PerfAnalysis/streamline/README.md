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
