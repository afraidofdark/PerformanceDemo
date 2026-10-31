# Stage 3 - two cameras, one frame: the display shows frames the app never drew

The stage 2 present path (composite straight into the window, no blit) made the frame three times
faster and, on the reference device, made the picture flicker: after the view changed once, the
display kept falling back to the view from before the change. This is the measurement that says
which layer does it, and what that leaves as options.

## The test

`Codes/Game.cpp`, behind `debug.tk.twocam`:

- Camera B is the scene camera's own copy, standing **2 world units along A's local X axis**, so the
  two views differ by nothing but where the camera stands - same lens, same direction, same scene.
- For the first `debug.tk.twocamframe` frames (60) the viewport draws through A, and through B on
  every frame after that. The navigator is frozen, so nothing else in the frame can change.
- The scene is static, so every frame after the switch has to repeat the one before it. One change in
  the picture is correct; two pictures trading places is the bug.

Mode `3` sets camera B up but keeps drawing through A, which separates "the switch" from "the second
camera exists in the scene".

### How the display is measured

`adb shell screenrecord` for the whole run, then every recorded frame is matched against two
reference stills, one of view A and one of view B (`classify-frames.py`). Each recorded frame is
classified A, B, black or other, and the number that matters is **how often view A comes back after
view B has been on screen**: zero is a pipeline that presents in order, anything else is a frame that
reached the display carrying older content.

Recording the display is deliberate. Reading pixels back inside the app is not an option on this
device - see "The app cannot sync its own present" below - and the compositor is the only observer
that sees what the panel is actually given.

## Result

Same device, same build of the app, only the engine's present path differs. `A-runs after B` is the
count of times view A reappears after the switch.

| present path | swap interval | A-runs after B, per run | frame |
| --- | --- | --- | --- |
| composite draws into the window (stage 2) | 0 (what the host asked for) | 13, 1, 7, 0 | 58-75 ms |
| composite draws into the window (stage 2) | 1 | 1, 1, 4, 1 | 58-75 ms |
| composite into a window sized target, then a **copy quad** into the window (stage 3) | 1 | 0, 1, 0, 1, 0, 1, 1, 1 | 58-62 ms |
| composite into a window sized target, then a driver **blit** (0.5.0) | 0 | 0, 0, 0, 0 | 149-180 ms |

Three things fall out of it:

1. **A heavy last command is what the swap races.** When the composite - a whole frame of shading -
   is the pass that writes the window, the display shows an older buffer in half of the runs badly
   (13 and 7 reappearances, spread over ~1.9 s). Put a cheap full screen copy in front of it and the
   worst case drops to a single frame, at the same 58-62 ms frame.
2. **The blit is the only path that never showed view A again (0 of 4 runs)**, and it is also the
   expensive one: about 90 ms of the frame, i.e. 6 fps instead of 16.
3. **An unpaced swap multiplies it.** The host asked for adaptive vsync, which EGL cannot do (see
   below), so the app was running at interval 0. Interspersed with interval 1 runs, the same build
   goes from 13/7 reappearances to 1/1/4/1, and a copy quad at interval 1 never goes above 1.

## Where the staleness is not

The obvious suspect is the render path drawing the old view again. It is not, and four independent
observations say so:

- **The engine logs the camera it draws with, once per change.** A run with a temporary log in
  `GameRenderer::Render` printed exactly two lines, the first frame and the switch, and nothing after
  it: the renderer used camera B on every frame of the window in which the display was flipping
  between A and B.
- **The view's render target hashes constant per camera.** Reading the target's own framebuffer
  between the frame and the swap (`hash` per frame) gave one hash for the whole A phase and one for
  the whole B phase, with the single intermediate value on the switch frame.
- **The GL state at present time is what it should be.** 127 of 127 sampled frames:
  `fbo=0 viewport=0,0,1080,2220 scissor=0 mask=1111`. The window is the draw target, the viewport
  covers it, nothing is masked or clipped.
- **The frame that reappears is a complete, clean render.** Comparing a reappearing frame against the
  view A still, band by band over the full 1080x2220, gives 1.5-3.3 of 255 per band: the whole
  picture is view A, not a torn or partially updated one.

So the picture is drawn, drawn correctly, and drawn into the right target - and the display still
shows an older one.

## The app cannot sync its own present

Three attempts to make the handoff explicit, all on the same device, all worse than doing nothing:

| what the app does between the frame and `SDL_GL_SwapWindow` | result on screen |
| --- | --- |
| `glReadPixels` from the window's back buffer (8 rows) | window black for the rest of the run |
| `glReadPixels` from the view's render target (4 rows) | window black for the rest of the run |
| `glFlush` | a half drawn frame: ground and a corner, rest black |
| `glFinish` | window black for the rest of the run |

The `glFlush` row is the informative one: submitting the command stream and then swapping shows the
frame *partially drawn*, which means the buffer reaches the display while the GPU is still writing
it. On this driver the swap does not wait for the frame, and any attempt to wait on the app's side
leaves the window with nothing in it. There is no app side fix in this direction.

## The host's adaptive vsync request is a silent downgrade

`Codes/Main.cpp` asks for adaptive vsync first, which is a WGL/GLX concept:

```cpp
if (!SDL_GL_SetSwapInterval(-1)) // Try adaptive VSync.
```

EGL clamps the interval to its own range, so `-1` lands on **0**, and SDL reports 0 back - the app
runs with no vsync at all while believing it got adaptive. That is fixed in the demo host: mobile
builds ask for `1` outright, and the interval actually in use is logged at startup
(`Present: swap interval in use is N`) because it is the first thing every stale-picture report
needs to know.

## A second device settles the "is it the driver" question

The same fast present path (composite into a window sized target, then one full screen copy quad) was
run on a second phone, a different vendor and a different gpu generation:

| device | gpu | android | fast path: A-runs after B, per run |
| --- | --- | --- | --- |
| SM-G950F | Mali-G71 MP20 | 9 | 0, 1, 0, 1, 0, 1, 1, 1 |
| TECNO CL7 | PowerVR BXM-8-256 | 16 | 0, 0, 0, 0, 0 |

Five clean runs on the second device is the strongest single result here: the fast path is not
inherently wrong, the draw-into-the-window handoff is fine where the driver implements it, and the
stale frames on the SM-G950F are that device's (2017 Mali driver, Android 9) behavior. It also means
the shape of the fix is a per platform decision and not an engine rewrite:

- a platform that presents a drawn back buffer correctly - the PowerVR/Android 16 device, and by
  extension anything current - keeps the fast path and its 58-62 ms frame.
- the SM-G950F generation needs the blit, at 171-197 ms.

Both devices are GPU bound on this scene (6.3 fps on the PowerVR device at 1080x2436, 5.9 fps on the
Mali one with the blit), so the frame time differs but the ordering does not.

## What the fast present is worth, per device

The stage 2 claim was that removing the full window blit is what takes the SM-G950F from 162 ms to
59 ms. That is true there and it is not true everywhere, so the same two paths were measured back to
back on the PowerVR device, warm, with the same static view:

| device | present | median frame | fps | swap wait |
| --- | --- | --- | --- | --- |
| SM-G950F / Mali-G71 / Android 9 | composite straight into the window | 58-62 ms | 16-17 | 3-4 ms |
| SM-G950F / Mali-G71 / Android 9 | composite into a target, then a blit | 171-197 ms | 5.1-5.9 | 59-64 ms |
| TECNO CL7 / PowerVR BXM-8-256 / Android 16 | composite into a target, then a copy quad | 158.3, 159.3, 159.5 ms | 6.27-6.31 | 119-123 ms |
| TECNO CL7 / PowerVR BXM-8-256 / Android 16 | composite into a target, then a blit | 157.6, 159.7 ms | 6.26-6.35 | 122-124 ms |

Two runs of each on the PowerVR device land inside the run to run spread of one configuration (157.6
to 159.7 ms), so the full window blit costs it nothing measurable: its frame is bound in the scene and
the swap wait is ~120 ms in both shapes. The ~110 ms difference on the SM-G950F is that driver's blit
cost, not a general property of presenting through a copy, and the fast path buys exactly nothing on a
device that is not paying it.

Which is the argument for shipping the blit: it is the shape that is correct on every device measured
here, and on the device where the fast path would help, it is also the shape that breaks the picture.
The fast path stays one constant away for a platform that both pays for the blit and presents a drawn
back buffer correctly - neither device here is that platform.

## What was done about it

The stage 2 present change (composite straight into the window, no blit) was reverted to the 0.5.1
present path: composite in place, then `CopyFrameBuffer` to the window. That is the shape that never
showed a stale frame on any device measured here, and the frame time it costs is device specific - the
two sections above have both numbers.

Two things were kept from the investigation:

- the host's swap interval fix (`Codes/Main.cpp`), because an unpaced swap is not a present path this
  driver can be trusted with: the same build goes from 13 reappearances to 1 with it, and EGL cannot
  honour the `-1` the adaptive vsync attempt asks for.
- the two camera test itself, which is what made any of this visible.

A present path that is both fast on the SM-G950F and correct there was not found. If someone wants to
look again, the two candidate directions are a copy that is cheaper than the driver's blit on that
device, and a per platform choice of present shape rather than one path for all - the PowerVR device
shows the draw present is fine where the driver implements it.

## Settled: do not remove the blit again

The stage 2 present change (composite straight into the window, no copy) is reverted and this is not
worth reopening in the same shape. What was actually found, in one place, with the numbers:

1. **The SM-G950F's Mali-G71 driver (Android 9) is unstable when a frame is drawn into the window's
   back buffer.** The draw races the swap: the display is handed a buffer from an older frame. Measured
   with the two camera test - one camera change, nothing else moving - the view from before the change
   came back on screen 13 to 18 times per run when unpaced, and 1 to 4 times with a real swap interval.
   The copy never did (0 of 4 runs). The engine renders one correct image per frame; that was checked
   per frame from inside the renderer, not inferred.
2. **The copy costs this device about 110 ms of frame time** (162 ms against 59 ms with the draw). That
   is a real cost and it is the only reason the change was attempted.
3. **On a PowerVR BXM-8-256 (TECNO CL7, Android 16) the copy is free** - 157.6 and 159.7 ms against
   158.3, 159.3 and 159.5 ms for the draw, i.e. the two are inside the run to run spread - and both
   shapes present correctly (7 clean runs). So the 110 ms is that one driver's blit, not a general
   property of presenting through a copy.
4. **Nothing on the app side can make the draw safe on the Mali device.** A `glReadPixels` from the
   window or from a render target, a `glFlush`, and a `glFinish` between the frame and the swap each
   leave the window black or half drawn there.

Which makes the decision independent of the device: the copy is correct everywhere measured, free on
the device where the draw is correct, and 110 ms on the device where the draw is not. Removing the copy
trades a real correctness bug on one device for no gain on the other. If it is ever revisited, the
thing to beat is the Mali blit cost itself (a cheaper copy, or a per platform present shape), not the
copy in general.

## Vsync: what a run measures

`debug.tk.vsync` selects it and the interval in use is logged at startup. On the scene here it does not
change the throughput number, measured on the TECNO CL7 at full resolution:

| vsync | median frame |
| --- | --- |
| 1 (paced, default) | 158.3 ms (6.32 fps) |
| 0 (unpaced) | 161.3 ms (6.20 fps) |

Both are GPU bound at ~150 ms, far past the panel's 16.7 ms, so pacing has nothing to pace. That stops
being true the moment a configuration gets close to the refresh: at that point `0` is what a performance
number needs (otherwise the panel's fps is what gets measured) and `1` is what any visual comparison
needs (an unpaced driver may queue frames ahead of the display, which is exactly how the stale buffer
above reaches the screen).

## Repeating it

```bash
# Build and install the app (see the PerformanceDemo README), then:
adb shell setprop debug.tk.twocam 1
adb shell setprop debug.tk.twocamframe 60     # frames of view A before switching to B
adb shell "screenrecord --time-limit 16 --bit-rate 20000000 /sdcard/twocam.mp4" &
sleep 1; adb shell am start -n com.otyazilim.toolkit.PerformanceDemo/com.otyazilim.toolkit.ToolKitActivity
sleep 18; adb shell am force-stop com.otyazilim.toolkit.PerformanceDemo
adb pull /sdcard/twocam.mp4 .

ffmpeg -i twocam.mp4 -vf scale=135:278 -pix_fmt gray -f rawvideo twocam.raw
python3 PerfAnalysis/twocam/classify-frames.py twocam.raw    # add the A/B reference stills
```

Run it several times per configuration. The reappearances are a race: one run out of four shows none
even with the direct write, so a single run proves nothing.

`PerfAnalysis/twocam/` keeps the `TWOCAM`, `PERF` and `PRESENT` lines of the runs quoted above.
