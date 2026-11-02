# SSAO: a pass that sampled an incomplete texture
## Ambient occlusion: a pass that sampled an incomplete texture

The occlusion term is multiplied into the ambient lighting before anything reaches the screen, so a
broken map looks like "the frame got darker", never like a bug in a pass. This is what it took to
turn that into a number, and what the number turned out to be.

`debug.tk.ssaodump 1` writes the pass's own targets out: the raw map, the blurred one the forward
pass samples, and the normal and depth target it reads, with the range of every channel of the float
target logged next to it.

The map was flat: mean 0.89, standard deviation 28, range 186 to 255, and pinning the pass's dither
made it a constant - every pixel counted the same four samples out of thirty two. Writing the sampled
depth into the map instead showed zero everywhere, while reading the same texture back with
glReadPixels returned the normal in RG and a linear depth of 0 to 38 metres in B. The input was fine;
the sampler was not. The normal depth target had never been given its filter, so it kept GL's default
min filter, which is a mipmap filter, and a target with no mip chain and a mip filter is an incomplete
texture - which samples as zero.

SSAOPass now states the sampler it needs (nearest, clamp to edge) on the texture it reads. The map
goes to a standard deviation of 41 and a range of 0 to 195, and the frame darkens by 11 percent with
the occlusion in the corners instead of a veil over everything.

| | mean | sd | range | frame luma |
| --- | --- | --- | --- | --- |
| broken | 0.89 | 28 | 186-255 | 117.4 (off) / 114.9 (on) |
| fixed | 0.46 | 41 | 0-195 | 117.4 (off) / 104.0 (on) |

`ssao-broken-map.png` and `ssao-fixed-map.png` are the two maps, `ssao-off-frame.png` and
`ssao-on-frame.png` the frames they produce.
