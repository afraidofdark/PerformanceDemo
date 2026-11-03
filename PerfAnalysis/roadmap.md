# Performance roadmap - where the time goes and what to try next

Written 2026-11-02. This is the working list, not a report: the reports are the per-topic files
next to it (`stage1-bottleneck-report.md`, `stage3-two-camera-report.md`,
`ssao-incomplete-texture.md`). Numbers here were re-measured on the same device with the same
method, so they can be compared with each other.

## The baseline

Everything below is measured against this, and this is what a change has to beat **without making the
picture worse** - the project's own settings, no probe overrides:

| setting | value |
| --- | --- |
| scene | `small-scene.scene` |
| render scale | 0.85 -> 918x1887 out of 1080x2220 |
| frame rate target | 0, unlocked (the engine gates the frame on this value) |
| MSAA | x2 |
| FXAA | off (the scene's own setting) |
| shadows | on, 2 cascades, distances 23.84 / 50.0, parallel split lambda 0.09 |
| shadow softening | `ShadowPCF` off, the variance blur at 5x5 with 2 taps |
| ambient occlusion | on, radius 1.0, bias 0.025, strength 1.0, occlusion target at the pre process size, pre process at half size |
| bloom | 5 iterations |
| tonemapping | on |
| depth of field, reflections | off |

What that costs, one cold start, twelve seconds of frames:

```
fps 13.7   period 72.8 ms   engine cpu 67.8 ms   gpu 59.2 ms   draws 211   passes 15
```

**The frame is now cpu bound**: the engine spends 67.8 ms submitting a 59.2 ms gpu frame. That is the
opposite of where this file started (a 162 ms frame that was entirely gpu), and it changes what to
optimise: a draw call, a program switch and a uniform upload are now worth more than a texel.

The per pass numbers for this baseline, from the gpu scopes:

| scope | time |
| --- | --- |
| ShadowPass (parent) | 38.7 ms |
| Directioal Shadow Map (inside it) | 2.5 ms |
| Shadow Blur (inside it) | 4.4 ms |
| Gamma Pass | 8.2 ms |
| CubeMapPass | 7.1 ms |
| ForwardPreProcessPass | 5.0 ms |
| GammaTonemapFxaaPass | 1.5 ms |
| ForwardRenderPass | 0.9 ms |
| CopyTexture | 0.7 ms |

Read the parent with care: a scope measures the gap between two timestamps on the gpu stream, so a
pass that waits on a cpu bound frame reports the wait as its own time. `ShadowPass` reports 38.7 ms
while the map render and the blur inside it report 2.5 and 4.4 - the other thirty is the gpu idling
while the cpu submits. Nested scopes are the real work; parents on a cpu bound frame are not.
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

## The frame back then (80 percent render scale, shadows off, scene draws)

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

## The list as it stood before the pass floor was measured

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
## The plan: same picture, fewer milliseconds

The rule is now fixed: nothing on this list may look worse than the baseline above. That rules out the
obvious dials (lower resolution, MSAA off, cheaper occlusion, fewer cascades) and leaves the work that
is invisible in a frame.

The frame splits into a 67.8 ms cpu side and a 59.2 ms gpu side, so each item is filed under the side
it helps. Ordered by what there is to gain.

### Cpu side - submission, 67.8 ms for 211 draws and 15 passes

1. **Fewer draws.** 211 draws for 159 visible objects, 76 of 238 instanced. A draw costs roughly 0.3 ms
   of cpu in this frame, so merging static geometry and raising the instanced share is worth more than
   anything on the gpu list. Measure first: how much of the 211 is the shadow pass (two cascades of the
   same casters) and how much the frame.
2. **Per draw overhead.** Jobs are drawn in scene order, so consecutive draws switch program, material
   and uniform buffers rather than sharing them. Sorting opaque jobs by program and material, and
   caching the bindings across a pass, is invisible in the picture by construction.
3. **Shadow pass draws.** It renders the casters once per cascade. Anything already culled per cascade
   should stay culled; anything drawn twice for the same cascade is pure loss.
4. **Pass count.** Fifteen passes, and each one costs a framebuffer bind and a descriptor flush on the
   cpu side as well as what it costs on the gpu. Merging is cheap to try and invisible if the pixels
   come out the same.

### Gpu side - 59.2 ms

5. **The post chain.** Gamma Pass 8.2, cube map 7.1, the pre process 5.0, tonemap 1.5, a copy 0.7.
   Bloom already runs on a pyramid; the copy and the gamma pass are separate full screen passes that
   could be one. Nothing here changes the picture if the maths is kept.
6. **The pre process pass, 5.0 ms.** Draw bound, not fill bound: making it depth only bought 0.3 ms, an
   RGBA8 target 0.3 ms. The draws are the cost, so a screen size cull (a surface a few pixels across
   contributes nothing to occlusion) is the lever - and it is invisible, which is what this list is for.
7. **The shadow atlas and its blur, 2.5 and 4.4 ms.** The blur already stopped being the expensive
   half; what is left is the caster draws, which is the same list as item 3 on the cpu side.
8. **MSAA x2 resolve.** Not measured on its own yet. Resolves are per target and per frame, and the
   picture depends on them, so only restructure this if the numbers ask for it.

### Not on the list any more, and why

- **Lower resolution, MSAA off, cheaper occlusion, fewer cascades** - all visible, so all out by the
  rule above.
- **Shadow map reuse for a static scene** - dropped by request.
- **Depth only pre process target, RGBA8 target** - measured at 0.3 ms each, not worth the code.
- **Reading the occlusion grid back inside the frame** - 51 to 21.8 fps, the read waits for the whole
  queue.
- **pcf taps** - done: four taps to none was 32 ms, and the variance blur already softened the edge.

## What the scale sweep and the cpu profile changed

Two measurements were run against the baseline before touching anything else.

**Scaling the render resolution** (a diagnostic only, the picture moves with it) separates the part of
the frame that follows pixels from the part that does not:

| render scale | frame | fps | engine cpu | gpu |
| --- | --- | --- | --- | --- |
| 0.85, the baseline | 79.6 / 70.7 ms | 12.6 / 14.2 | 85.9 / 79.1 | 58.0 / 57.2 |
| 0.60 | 60.0 ms | 16.7 | 48.8 | 44.4 |
| 0.40 | 46.2 ms | 21.6 | 50.5 | 30.0 |
| 0.20 | 42.7 ms | 23.4 | 33.7 | 39.1 |

Pixels fall by eighteen times between 0.85 and 0.20 and the frame only falls by half: there is a **floor
around 42 ms that does not care about resolution**, and 211 draws and 15 passes are what it is made of.
At the baseline the gpu still costs more (58 of 73 ms), so both sides matter, but the floor is what will
stop us: no gpu optimisation can take the frame under it.

**The cpu profile** says where that floor is:

| scope, per frame | inclusive | exclusive |
| --- | --- | --- |
| `Render` | 117.5 ms | 11.1 |
| `PreRender` | 53.9 ms | 0.6 |
| **`SetFramebuffer`** | - | **20.6 ms** |
| `RenderBatch` | 9.6 ms | 0.3 |
| `RenderShadowMaps` | 8.2 - 10.8 ms | 0.1 |
| `RenderShadowCasters` | 10.7 ms | 1.8 |
| `FeedUniforms` | - | **6.7 ms** |
| `BlurShadowAtlas` | 5.2 ms | 0.7 |
| `RenderOpaque` | 5.0 ms | 1.9 |

`SetFramebuffer` at 20.6 ms is not binding: the backend already caches framebuffer ids. It is the bind
plus the clear, thirty-odd times a frame - bloom alone is ten sub passes, the shadow blur four. Each of
those is a full screen clear of a target that is often about to be overwritten entirely.

**Where the shadows go** (frame deltas against the baseline, not scopes): of the 43 ms the shadows cost,
the atlas render is 15 to 20 ms across 119 draws, the per pixel term 20 to 28, and the blur 7.

**And the drawer of the 119 draws** is not a batching failure: the scene has 140 casters but only **49
distinct meshes**, so instancing tops out around fifty batches per cascade and it is already there.
Fewer draws from here means fewer meshes, which is the merging that was ruled out.

## So the order of work is

1. **Cuts that cost no picture: the clear and bind budget.** A target that a pass overwrites in full
   does not need clearing first, and passes that only read do not need a bind at all. That is `20.6 ms`
   with nothing visible to lose, and it is the largest single line in the cpu profile.
2. **`FeedUniforms`, 6.7 ms.** One upload per draw is still one upload per draw; sorting batches so
   consecutive draws share a program and a material lets most of them go.
3. **The shadow cpu work, 9 to 11 ms** of culling and job packing per frame, twice per cascade.
4. Only then the gpu side, because the floor caps whatever the gpu savings buy.
### How to measure a candidate

One cold start each, interleaved A B A B, and read `PERF fps=` plus `engcpu` and `gpu` from the same
line. The rule is that the frame period drops and the picture does not: capture a screenshot of each
configuration with the camera in the same place and diff them, as `shadow-pcf-vs-vsm.md` did. Anything
above about 1 percent of pixels differing needs a reason.
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
