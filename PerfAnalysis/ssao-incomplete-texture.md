# SSAO: two sampler bugs, one of them only on the phone

The occlusion term is multiplied into the ambient lighting before anything reaches the screen, so a
broken map looks like "the frame got darker", never like a bug in a pass. Getting from that to a
cause took three separate measurements, and the first one pointed at the wrong culprit.

`debug.tk.ssaodump 1` writes the pass's own targets out - the raw map, the blurred one the forward
pass samples, and the normal and depth target it reads - with the range of every channel of the float
target logged next to it.

## What the map said

Flat: mean 0.89, standard deviation 28, range 186 to 255. Pinning the pass's dither made it a
constant, so every pixel counted the same four samples out of thirty two. Writing the sampled depth
into the map instead showed zero everywhere - while reading the same texture back with glReadPixels
returned the normal in RG and a linear depth of 0 to 38 metres in B. The input was fine; the sampler
was not.

## The first cause: a target with no filter

The normal depth target was never given its filter, so it kept GL's default min filter, which is a
mipmap filter, and a target with no mip chain and a mip filter is an incomplete texture, which
samples as zero. SSAOPass now states the sampler it needs (nearest, clamp to edge) on the texture it
reads, which is also what the maths wants: RG carries a normal and B a depth, so a filtered fetch
averages a normal into a different normal.

## The second cause, and the one that mattered on the phone

With that fixed the map came alive on the desktop and stayed flat on the phone. The difference was
`GLBackend::BindTextureDirect`: it skipped `glBindTexture` when its own record said the texture unit
already held that texture, and the record can be wrong, because the binding is global state that other
paths touch. Moving the pass's input to a unit nothing else used made the map correct immediately on
the same device, which is what identified it. The skip is gone; the pass is back on the slot its
shader declares.

## Result

| | mean | sd | range |
| --- | --- | --- | --- |
| flat | 0.89 | 28 | 186-255 |
| real | 0.95 | 28 | 31-255 |

The mean moved up because the sky is now excluded rather than read as fully occluded: a pixel the
prepass did not draw has no depth to reconstruct from, and its samples projected to infinity before
this. The occlusion itself lives in the window recesses and along the building edges, which is what
`ssao-fixed-map.png` and `ssao-on-frame.png` show. `ssao-broken-map.png` and
`ssao-off-frame.png` are the same view before the fix.