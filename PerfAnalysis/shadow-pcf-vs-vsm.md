# Shadow softening: pcf taps against the variance blur

Measured 2026-11-02 on SM-G950F (Exynos 8895, Mali-G71 MP20), 80 percent render scale, MSAA off,
occlusion on, shadows on, one cold start per configuration and two runs of the two configurations
that matter to show the drift. Screenshots are `shadow-*.png` next to this file.

Both settings are ways to soften the same shadow edge and the engine ships with both on: the shadow
pass blurs the variance moments (`VSMBlurKernelSize` 5x5, `VSMBlurTapCount` 2), and the forward
shader then takes four bilinear taps of that already blurred atlas (`ShadowPCF` 4). The question was
whether the second one is buying anything.

Knobs used, added to the probe for this: `debug.tk.shadowpcf` (0 off, 1 four sample, 2 nine sample,
3 sixteen sample), `debug.tk.vsmtaps` (0 off, 1 to 5), `debug.tk.vsmkernel`.

| configuration | frame | fps | mean luma |
| --- | --- | --- | --- |
| pcf 4, blur off | 83.20 ms | 12.02 | 105.32 |
| pcf 4, blur off, again | 86.17 ms | 11.61 | 105.82 |
| **pcf off (one tap), blur 2** | **52.77 ms** | **18.95** | 106.01 |
| **pcf off (one tap), blur 2, again** | **52.75 ms** | **18.96** | 106.12 |
| pcf off (one tap), blur 3 | 54.90 ms | 18.21 | 106.64 |
| pcf 4, blur 2 (the shipped default) | 87.77 ms | 11.39 | 106.08 |
| pcf 9, blur off | 165.29 ms | 6.05 | 105.88 |

## What it costs

- **One tap instead of four is worth 32 ms**, and dropping the blur on top of four taps is worth
  another 3. The shipped pair, both on, is the most expensive arrangement there is: 87.77 ms against
  52.77 for the cheap one, so **11.4 fps becomes 19.0**.
- One pcf tap is about **10.6 ms** at this size (32 ms over three taps). The nine sample setting
  puts five more taps on and lands at 165 ms, which is 15.6 ms a tap - the cost grows faster than
  the tap count, so this is not a steady per fetch price.
- The four taps and the blur are redundant by construction: the atlas already holds blurred
  variance moments, and a single bilinear fetch of those is a smooth value. Four taps of a smooth
  value average to nearly the same smooth value.

## What it looks like

Mean luma across all seven configurations sits within 1.3 of 105.3 to 106.6, so nothing is losing
its shadows. Pixel difference against the shipped default, sampled over the frame:

| comparison | mean difference (of 765) | pixels differing by 24 or more |
| --- | --- | --- |
| pcf 4 blur off against pcf off blur 2 | 2.88 | 1.66 % |
| shipped default against pcf off blur 2 | 3.16 | 1.66 % |
| pcf 4 blur off against pcf 9 blur off | 2.23 | 0.95 % |
| pcf off blur 2 against pcf off blur 3 | 2.14 | 0.26 % |

The 1.66 % that moves is the shadow terminators: the four sample setting puts a slightly wider
penumbra there. `shadow-b-pcf0-blur2.png` shows the cheap configuration - soft edges on the ground
and under the fire escape, no stair stepping, no acne.

## Conclusion

The blur and the taps should not both be paid for. `ShadowPCF` off with the variance blur on is
**32 ms cheaper and visually within a percent and a half of pixels** of the shipped pairing, which
on this scene is the difference between 11 and 19 fps. Making the two mutually exclusive at the
setting level would stop a project from paying twice for the same softness.

Where the money would go afterwards: the blur itself is only 3 ms, so the shadow cost left is the
atlas render (about 25 ms, see `roadmap.md`). Nothing here helps that.

## Reproducing

```
setprop debug.tk.res 80
setprop debug.tk.msaa 0
setprop debug.tk.ssao 1
setprop debug.tk.shadowpcf 0    # off; 1 is four taps, 2 nine, 3 sixteen
setprop debug.tk.vsmtaps 2      # 0 off; 1..5 taps
```

One cold start per configuration, twelve seconds of frames, and read the `PERF fps=` line. The
phone has to stay awake or the render loop stops and the probe prints nothing.
