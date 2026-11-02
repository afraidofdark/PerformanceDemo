# Performance roadmap - where the time goes and what to try next

Written 2026-11-02. This is the working list, not a report: the reports are the per-topic files
next to it (`stage1-bottleneck-report.md`, `stage3-two-camera-report.md`,
`ssao-incomplete-texture.md`). Numbers here were re-measured on the same device with the same
method, so they can be compared with each other.

## How these were measured

Device SM-G950F (Exynos 8895, Mali-G71 MP20), Release build, one cold start per configuration,
app force-stopped between runs, `--pack` before every build so the pak is never stale.

Two things that cost us time and are worth remembering:

- The phone sleeps. If the screen is off the activity is paused, the render loop stops, and the
  probe prints nothing at all. Wake it first (`input keyevent KEYCODE_WAKEUP`, `svc power stayon
  true`).
- Consecutive runs drift with heat: a sequence of three identical runs read 60, 53, 47 fps once.
  Any A/B has to be interleaved (A, B, A, B) or it measures the SoC temperature, not the change.

Per-pass gpu times come from the timestamp queries behind `debug.tk.passgpu`
(`ToolKit/Render/OpenGL/GLBackend.cpp`), shown in the editor under Stats or dumped by the probe as
`PROFGPU` lines. They have a floor of roughly one to two milliseconds on this driver, so read the
*differences* between configurations, never one absolute number on its own.

## The frame today (80 percent render scale, shadows off, scene draws)

| configuration | frame | fps | draws |
| --- | --- | --- | --- |
| nothing on | 16.67 ms | 59.99 (vsync) | 53 |
| + ssao | 19.95 ms | 50.12 | 93 |
| + shadows | 81.84 ms | 12.22 | 146 |
| + shadows and ssao (project baseline) | 93.45 ms | 10.70 | 186 |

At the project's own baseline settings (1080x2220, MSAA x2, shadows and ssao on) the frame is
148 ms, 6.7 fps.

## What a pass costs, and the surprise

Half of the answer is in this table, measured with the geometry hidden so the passes have nothing
to draw:

| pass | with 93 draws | with no geometry |
| --- | --- | --- |
| ForwardPreProcessPass | 4.31 ms | **2.51 ms** |
| SSAOPass | 2.27 ms | 2.47 ms |

A pass that draws nothing still reports 2.5 ms, and the occlusion pass costs the same whether the
scene is drawn or not. Two readings, and they decide everything that follows:

1. It is an artifact of the query method: an empty `ShadowPass` also reports 1.05 ms, and the
   frame timer had to be turned off while the scope queries run because the two interfere on this
   driver.
2. It is real: every pass pays for its tiles being stored and reloaded, and a frame with ten to
   fourteen passes pays it ten to fourteen times.

Nothing else in this list matters as much as that question, because it is worth up to 30 ms.

## The list, ranked

1. **Pass overhead** - see above. One experiment settles it (below).
2. **Shadows, 65 ms** - 53 draws and 16.67 ms becomes 146 draws and 81.84 ms. Inside it: the
   shadow map pass drawing every caster, and a forward shader compiled with `ShadowPCF=4`, which
   is twelve texture fetches a pixel. Never broken down per pass yet.
3. **The post chain, about 11 ms** at 80 percent: bloom 3.7, gamma/tonemap/fxaa 2.5, the scene
   copy 2.6, full quad 2.5. Pass merging or a cheaper bloom start.
4. **The pre process pass's draws, about 1.8 ms** - 93 draws at roughly 24us each; the pass is
   draw bound, not fill bound. A screen size cull (a surface a few pixels across contributes
   nothing to occlusion) would cut the count.
4b. The pre process pass's shader is *not* the cost: making it depth only - no colour write, no
   normal map fetch, no half float target - measured 4.69 to 4.41 ms. Bandwidth is not the driver
   either: an RGBA8 target instead of RGBA16F is worth about 0.3 ms. Both were tried and rejected.
5. **Occlusion calc and blur in one pass, 0.5 to 1 ms** - two sub passes, two framebuffer switches.
6. **Startup warmup, 1.9 s** - not frame time, but it is what a user sees: `GenerateSpecularEnvMap`
   reports 996 ms and its mip 0 alone 590 ms, in the per pass gpu dump.
7. **Occlusion culling from a fresher grid** - the grid is reduced at the end of the frame and read
   at the top of the next one, so it is a frame old and pops on camera movement. Reading it back
   inside the frame was tried and is unusable: 51 to 21.8 fps, because `glReadPixels` waits for
   everything the driver has queued, not just the pass before it.

## The pass floor was measured: it is the measurement, not the work

Two passes were added to the demo behind `debug.tk.nullpass`, both driven through the same
`Stats::BeginGpuScope` the real passes use: one clears a 1x1 target, the other a frame sized one.
Neither draws anything.

| | fps | period | scope says |
| --- | --- | --- | --- |
| reference, no extra pass | 48.17 | 20.76 ms | |
| + 1x1 clear | 50.23 | 19.91 ms | **NullPass1x1 2.45 ms** |
| + 1x1 and full size clear | 44.84 | 22.30 ms | **NullPass1x1 2.59 ms, NullPassFull 2.61 ms** |

A pass that clears a single pixel reports the same 2.5 ms as one that clears 2.4 million. Fill is not
the cost, the clear is not the cost, and the two-tenths between them is noise. What the scope measures
is the gap between two timestamps on a queued stream, and on this driver that gap has a floor of about
two and a half milliseconds that has nothing to do with the pass inside it.

The frame column disagreed with itself on that, which is why the counters were asked. Two captures with
the same scene, one carrying both null passes and one carrying neither:

| | gpu active | tiler active | frag active | clock | frame |
| --- | --- | --- | --- | --- | --- |
| no null pass | 85 % | 22 % | 75 % | 544 MHz | 20.24 ms |
| two null passes | 80 % | 21 % | 72 % | 533 MHz | 20.11 ms |

Two extra passes leave the gpu's own counters where they were - they are, if anything, a hair lower,
which is the clock and the run, not the passes. So the passes cost no gpu work at all: there is no
tile work, no fragment work, nothing. The 2.5 ms each one reports is entirely the timestamp method, and
the frame delta of one earlier run (+1.5 ms for two passes) was noise.

What that means for reading this file:

- Every per pass number is inflated by about 2.5 ms and is only good as a **difference between
  configurations**. The differences are sound because the floor cancels, which is why the earlier A/Bs
  still hold.
- The pass structure is not the frame. Ten to fourteen passes at a *fixed* cost we cannot measure this
  way are not the thirty milliseconds the inflated numbers suggested, and they are not the eight to
  eleven either.
- To know what a pass really costs, measure the frame with and without it, interleaved, or read the
  counters, not the scope. That is what the shadow breakdown below has to do.
## Shadow breakdown, measured with knobs

Three settings were given probe knobs (`debug.tk.cascades`, `debug.tk.denseslots`,
`debug.tk.shadowpcf`) and measured against each other, 80 percent render scale, MSAA off,
occlusion on, shadows on. Frame period, two runs of the baseline to show the drift:

| configuration | period | fps |
| --- | --- | --- |
| 4 cascades, 1K atlas, pcf 4 (baseline) | 97.04 ms | 10.30 |
| 2 cascades | 95.85 ms | 10.43 |
| 2 cascades, half resolution slots | 92.07 ms | 10.86 |
| **pcf off, single sample** | **45.67 ms** | **21.90** |
| 2 cascades, pcf off | 47.02 ms | 21.27 |
| baseline again | 96.57 ms | 10.35 |

What that says, and it is not what the first reading of the same data said:

- **The per pixel pcf sampling is the shadow cost: about 48 ms.** Turning it off takes the
  frame from 97 to 46 ms.
- The atlas render behind it is about **25 ms** - the other subtraction: 46 to 20 ms, and 68 ms
  with the casters cleared but the sampling kept agrees with it.
- **Cascade count is worth 1.2 ms** (4 to 2) and halving the atlas slots about 3.8 ms. Both are
  noise next to the sampling, which kills the obvious "fewer cascades, smaller atlas" plan.

The earlier version of this section credited the map pass with 48 ms and the sampling with 27,
which came from subtracting the two configurations in the wrong order. The `shadow 0` knob
clears the per light caster flags, so the atlas stops being rendered while the shader keeps
sampling it - that configuration measures the *sampling*, not the map.

Answered since: the taps and the variance blur are redundant and paying for both costs 32 ms.
See `shadow-pcf-vs-vsm.md`. With `ShadowPCF` off and the 5x5 blur on, the frame goes from 87.8 to
52.8 ms (11.4 to 19.0 fps) with 1.66 percent of pixels differing, which is the penumbra width.

So the shadow work starts in `lighting.shader`, not in the shadow pass. It does not need `malioc` after all:
the tap count was the whole story, and the fix is to stop asking for taps on top of a blurred atlas.
## What that leaves

The list above stands with one change of order: the pass floor is answered, so the frame is mostly
the scene passes themselves.

1. **Shadows, 65 ms** - now unambiguously the largest single item, and still unbroken down per pass.
   Start there: the shadow map pass, then the per pixel cost of `ShadowPCF=4` in the forward shader.
2. **The post chain, 8 to 11 ms of real pass cost** - bloom, gamma/tonemap/fxaa, the scene copy.
3. **The pre process pass, 4.3 ms with 93 draws** - draw bound; a screen size cull is the lever.
4. Occlusion calc and blur merged, ~0.8 ms.
5. Startup warmup, 1.9 s, not frame time but what a user sees first.
## State when this was written

Both repositories are clean and pushed. The work in progress lives in stashes:

```
engine  stash@{0}  wip: ssao normals from depth, ssaonormalsfromdepth setting, spread removal, resolve guards
demo    stash@{0}  wip: ao normals from depth knob
```

It adds `PostProcessingSettings::SSAONormalsFromDepth` (ambient occlusion derives its normals from
the target's depth channel instead of reading a normal map, forced off while reflections or depth
of field are on), removes the meaningless `SSAOSpread` setting, and fixes three crashes found on
the way:

- a depth only pre process target has no colour attachment, and the resolve asked for one anyway;
- a multisampled depth attachment is a renderbuffer, so the occlusion pass was handed a null
  resolved texture;
- `ResolveFramebuffer` asserted on an attachment that does not exist, which is a null dereference
  in release builds.

Measured with that work: occlusion from the depth channel is 49.33 fps against 49.97 with the g
buffer normals, and visibly weaker but correct. It is a quality knob, not a performance one - the
pre process pass keeps costing what it costs because it is draw bound.
