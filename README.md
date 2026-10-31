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
```

Two of the numbers above need engine support that a plain GDTK checkout does not have: the real
GPU frame time (`GL_EXT_disjoint_timer_query` on GL ES) and the per-pass profiler breakdown. Both
live in the engine commit `feat(toolkit): report a real gpu frame time on gl es`; without it the
gpu column reads 1.00 ms and the profiler only reports the occlusion culling scopes.
