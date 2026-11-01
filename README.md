# PerformanceDemo

A ToolKit (GDTK) game project used to measure where a frame's time goes on a phone, plus the
measurement that came out of it.

The scene is `Resources/Scenes/small-scene.scene`: a city block from the Polygon City pack, one
directional light, one local light, tonemapping + bloom + SSAO enabled, MSAA 2x, HDR pipeline on.
On the reference device (Galaxy S8, Exynos 8895 / Mali-G71 MP20, 1080x2220) it renders at 6.2 fps.

## What is in here

```
Codes/          game sources: the host (Main.cpp), the game plugin (Game.cpp, FpsNavigation.*)
                and PerfProbe.h, the per-frame probe the measurements used
Config/         per-platform engine settings (Android/Windows/Linux/Web)
Resources/      the scene and everything it references: city meshes, materials, textures,
                prefabs (44 MB)
Android/        the gradle project the Packer drives
PerfAnalysis/   the stage 1 investigation: report, scripts, raw run logs per configuration
CMakeLists.txt  project build, consumes the GDTK engine as a subproject
```

## Building

The engine is not vendored. Point the project at a GDTK checkout, either through `Path.txt`
in the per-user ToolKit config directory (one line, the engine root) or the `TOOLKIT_DIR`
environment variable / cache variable. That directory is `%APPDATA%/ToolKit/Config` on
Windows and `$XDG_CONFIG_HOME/ToolKit/Config` (`~/.config/ToolKit/Config`) elsewhere.

Desktop:

```
cmake -S . -B ./Intermediate/Windows -A x64 -DTK_PLATFORM=Windows -DCMAKE_BUILD_TYPE=Release
cmake --build ./Intermediate/Windows --config Release
```

Android: run the engine's Packer from its `Bin<Config>` directory with a
`PublishArguments.txt` for this project (project name, workspace path, app name, sdk range,
platform 3 = Android). The Packer packs `Resources/` into `MinResources.pak`, regenerates the
gradle project and icons, runs the gradle wrapper and drops the APK in `Publish/Android`.
Build output, the pak and the gradle caches are ignored by git.

The gradle step needs the Android SDK, and AGP 8.1.1 pins its own toolchain: `ANDROID_HOME`
(the packer looks for `adb` in its `platform-tools`) and a JDK 17 as `JAVA_HOME`, because
Gradle 8.1.1 rejects newer JVMs. The native side is compiled by the NDK that AGP picks
(25.1.8937393) rather than by the desktop compiler, so the Linux build tree is not involved.
`deploy after build` = 1 in `PublishArguments.txt` installs and starts the APK on the
attached device.

## The two camera test

`Codes/Game.cpp` can render a view that provably does not change, which is what a present path has to
be judged on: a camera that moves every frame hides a stale buffer behind the motion, a camera that
moves once exposes it.

```
adb shell setprop debug.tk.twocam 1        # camera A for the first frames, then camera B forever
adb shell setprop debug.tk.twocamframe 60  # how many frames A gets (default 1)
```

Camera B is the scene camera's own copy standing 2 world units along its local X axis, so the two
views differ by nothing but where the camera stands. Nothing else moves - the navigator is frozen
while the test is on - so after the switch every frame has to repeat the one before it.

Record the display and classify every frame against a still of view A and of view B
(`PerfAnalysis/twocam/classify-frames.py`). Any frame of view A after the switch
is a buffer that reached the screen without being drawn for that frame. Mode `3` sets camera B up but
keeps drawing through A, which is the control.

Run it several times per configuration: the reappearance is a race, and one run in four shows none
even on the broken path.

The measurement and what it found are in `PerfAnalysis/stage3-two-camera-report.md`, next to the
frames classifier and the run logs the report quotes.

One result of that pass is not about the test: the host asked for adaptive vsync, and EGL clamps the
`-1` it asks for to `0`, which is no vsync at all. `Codes/Main.cpp` now takes the interval from
`debug.tk.vsync` and logs the one it ends up with.

**The present path is settled: do not remove the full window copy again.** It was tried (stage 2), it is
reverted, and the report's "Settled: do not remove the blit again" section has the numbers. In short:
the SM-G950F's Mali-G71 driver hands the display an older frame when a frame is drawn into the window
instead of copied, 13 to 18 times per run; that copy costs the same device about 110 ms of frame time;
and on the PowerVR device the copy is free and both shapes are correct, so removing it buys nothing
where it is safe and breaks the picture where it would help.

## Changing resources or settings

The app keeps what it unpacked from the resource pak in its own data directory, and it reuses that on the
next launch. Editing a scene, `Config/Engine.settings` or any other resource and reinstalling is
therefore not enough - the run still loads what the previous install left behind, and a settings change
looks like it was ignored. Clear the app's data between an install and a run that has to see the change:

```
adb shell pm clear com.otyazilim.toolkit.PerformanceDemo
```

The `PERF config` line in logcat says which settings a run actually used (`msaaUsed`, `fxaaUsed`, and the
post process values the scene contributed), which is what tells the two cases apart: a setting that did not
apply, or a run that is still on the old unpacked copy.

## Shader cost, statically

When the frame is bound by the forward shader rather than by draw count or resolution, `malioc` (Mali
Offline Compiler, from Arm Performance Studio) reports where inside it the time goes: ALU, load/store,
varying and texture cycles, plus register pressure. It needs the shader as one GLSL translation unit,
which `PerfAnalysis/malioc/assemble-forward-shader.py` produces from the engine's `.shader` files and
their includes:

```
PerfAnalysis/malioc/assemble-forward-shader.py ../../GitHub/GDTK/Resources/Engine/Shaders defaultFragment.shader > forward.frag
malioc --fragment -c Mali-G71 -D ShadowPCF=4 -D CascadeBlend=1 -D ShadingMode=1 \
       -D DrawAlphaMasked=0 -D highlightCascades=0 forward.frag
```

`PerfAnalysis/malioc/` keeps the output for the shipped variant and for one with the IBL term removed.
Measured on the reference device with shadows and SSAO off and no post processing, that split matches
the frame: 65 ms with the IBL term, 35 ms without it, and an albedo-only variant (which stops right
after the material fetch) reaching the 60 fps cap.

## Performance analysis

`PerfAnalysis/stage1-bottleneck-report.md` is the write-up, `PerfAnalysis/README.md` indexes every
run log.

Measured on the reference device, one cold start per configuration, app force-stopped between
runs:

| configuration | frame | fps |
| --- | --- | --- |
| stock | 162 ms | 6.2 |
| shadows off | 124 ms | 8.1 |
| all geometry hidden | 55 ms | 18.3 |
| 50 % render resolution | 68 ms | 14.5 |
| 25 % render resolution | 53 ms | 18.5 |

The GPU needs 140 ms of the 162 ms frame and the CPU sits at 36 % of one core, so the frame is GPU
bound, and the cost is per-pixel shading rather than colour bandwidth or vertex work. The report
has the decomposition and the ranked levers.

The runs are driven by `debug.tk.*` system properties, which `Codes/PerfProbe.h` reads at startup,
so an experiment needs no rebuild:

```
adb shell setprop debug.tk.res 50      render resolution percentage, 10..200
adb shell setprop debug.tk.shadow 0    lights stop casting
adb shell setprop debug.tk.mesh 0      every mesh entity hidden
adb shell setprop debug.tk.msaa 0      sample count on the render targets, 0 2 4 8
adb shell setprop debug.tk.fxaa 1      post pass antialiasing instead
adb shell setprop debug.tk.vsync 0     unpaced: the frame the app can produce, not the one the panel takes
```

**Vsync and what a run measures.** `debug.tk.vsync` decides it, and the interval that ends up in use is
logged at startup either way:

- `0`, unpaced, for a throughput number. Any pacing on and a configuration faster than the panel
  measures the panel: at 60 Hz nothing above 60 fps is visible, so an optimisation that goes from 8 ms
  to 4 ms looks like no change at all. On the scene here every frame is 58-200 ms, well past the
  panel's 16.7 ms, so the two settings give the same throughput today - the knob matters as soon as an
  optimisation, a lower render scale or a lighter scene gets close to the refresh.
- `1`, paced, for anything visual: a screenshot, a colour or FXAA comparison, or the two camera test.
  Unpaced, a driver is free to queue frames ahead of the display, and on the SM-G950F that is exactly
  how a stale buffer ends up on screen - the present path matters here, see the stage 3 report.

The default is `1` on mobile, because a picture that cannot be trusted makes every other measurement
harder to read.

Two of the numbers above need engine support that a plain GDTK checkout does not have: the real
GPU frame time (`GL_EXT_disjoint_timer_query` on GL ES) and the per-pass profiler breakdown. Both
live in the engine commit `feat(toolkit): report a real gpu frame time on gl es`; without it the
gpu column reads 1.00 ms and the profiler only reports the occlusion culling scopes.
