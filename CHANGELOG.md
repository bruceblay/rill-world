# Changes

## 0.2.0 (2026-09-29)

- Tap now moves to a different environment, and shake picks a new recording within the current one.
- Plays with other Rill devices nearby over ESP-NOW, with no setup, following the ensemble's tempo and bar line. World leaves keeping the clock to the others.
- A new texture comes in on the next shared bar line.
- Starts at the quietest volume, as the environmental layer under an ensemble.
- Dropped the rain-on-wheelbarrow clip to make room for the ensemble radio.

## Initial repository — Study 1

First scaffold: three filtered-noise textures (Water, Rain, Wind), a bar-synced swell standing in for a vintage sound conditioner's Surf Rate/Range knobs, and a step-scheduled transient layer for Rain's droplets and Wind's rustles. One ambient particle visual, drift direction and palette following the active texture. Host tests cover audio stability, a jump/discontinuity bound across texture crossfades, seed reproducibility, texture coverage, fade/resume, and shake detection.

Found and fixed a self-oscillating resonant filter on Rain's droplet transient: its color filter sat right at the Chamberlin state-variable filter's stability edge (5200 Hz center at 32 kHz, Q 1.3), so instead of a colored click it rang at a fixed high amplitude, alternating sign almost every sample. Tightened the filter's general stability clamp and moved Rain's droplet color down to 3400 Hz / Q 0.85. Also found that small integer seeds (1-20) all picked the same initial texture, an xorshift32 property where the first output correlates with a small seed; added a few throwaway RNG iterations before the first draw. Not yet run on physical hardware.
