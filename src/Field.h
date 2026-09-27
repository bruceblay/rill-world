// Copyright (c) 2026 Bruce Blay
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include "Samples.h"

// A bounded, deterministic playback engine shared by firmware and audition.
// No allocation, locks, or unbounded work in the per-sample path. Real field
// recordings (see docs/SOURCES.md), not generated noise -- an earlier
// from-scratch filtered-noise synthesis was tried first and rejected by ear
// as universally bad, the same conclusion Rill Drums reached about its own
// procedural noise voices.
//
// A continuous sine sweep on the filter, at a wide range, read as "the main
// event" instead of texture -- narrowed the range and slowed it down.
// A stepped, sample-and-hold version (a new random cutoff on a rhythmic
// pattern, held rather than swept) was tried in between and didn't work out
// by ear either; reverted to the sweep. Five punch-in effects (Pitch
// Wobble, Delay Throw, Crush, Reverb, Smear) still layer on top for variety
// across a longer sit-and-listen, the same direction Rill Drums' punch-in
// effects took: occasional, self-clearing, and evolving while active rather
// than one flat setting. Crush sweeps its hold depth in and back out rather
// than snapping to a fixed amount; Smear is Delay Throw's blurrier sibling,
// wobbling its tap length and damping each repeat so the echoes smear into
// the bed instead of reading as a discrete, clean echo.
//
// In an ensemble the conductor's tempo and bar count drive this bar clock
// (see Radio.h and SYNC-DESIGN.md in the Rill repository).
namespace field {
constexpr uint32_t rate = 32000;
constexpr float pi = 3.14159265358979323846f;
constexpr unsigned steps = 16;
// Four banks: five recordings of rain hitting different surfaces (umbrella
// cloth, puddle, concrete, terrace tile, a plastic tarpaulin -- a sixth, a
// metal wheelbarrow, was dropped to make room for the ensemble radio),
// three bird ambiences (forest, dawn chorus, evening), four
// insect recordings (nocturnal insects+wind, crickets/frogs meadow, a
// close field cricket, a lone night grasshopper -- a cicada song was tried
// and dropped as off-putting), and three ocean recordings (two wave
// textures, one underwater hydrophone recording -- a continuous rushing
// texture that loops cleanly, unlike two ocean-animal sounds tried and
// rejected before it: a humpback whale song (too low, read as weak) and a
// bottlenose dolphin (sharp repeated clicks, unpleasant on a loop). A
// Weddell seal trill was considered as a third try but dropped before
// ever reaching the device: unlike the whale and dolphin clips (both
// credited to NOAA's own Passive Acoustics Group and so clearly public
// domain), the seal recording was credited to an external university
// researcher merely hosted on a NOAA gallery page, with no license
// statement covering that hosting -- not established as freely usable.
// A body-sounds bank
// (heartbeat, breathing) was tried and dropped: not interesting enough to
// keep, and Ocean read as more distinct from the other three banks anyway.
// A fifth bank, Cave, has two digitally-composited underground ambiences
// (rain, drips and reverberation, built by BigSoundBank from field
// recordings rather than captured as one take) -- kept to two short clips
// for now, both quite dynamic (sharp drips against long quiet stretches),
// so their gain runs well above the other banks'. The engine and its
// effects are generic over "whatever clips are in the current bank," so
// each bank is just more Texture entries, more Character rows and a
// BankRange -- shake (newVariation()) only picks within the current bank,
// tap (newBank()) is the only thing that crosses a bank boundary.
enum Texture : unsigned { Rain = 0, RainPuddle, RainConcrete, RainTerrace, RainTarpaulin,
                           BirdForest, BirdWake, BirdEvening,
                           InsectNight, InsectCrickets, InsectFieldCricket, InsectGrasshopper,
                           OceanWaves1, OceanWaves2, OceanUnderwater,
                           CaveOne, CaveTwo, textureCount };
enum Bank : unsigned { BankRain = 0, BankBirds, BankInsects, BankOcean, BankCave, bankCount };
enum Punch : unsigned { PunchNone = 0, PunchPitchWobble, PunchDelayThrow, PunchCrush, PunchReverb, PunchSmear, punchCount };

class Engine {
  struct Character {
    float cutoffLowHz, cutoffHighHz; // sweep range (dark <-> open)
    float q, gain, swellBars;        // swellBars: bars per full breath
  };

  struct BankRange { unsigned first, count; };

  uint32_t rng, punchRng = 1;
  unsigned bank = 0, texture = 0, generation = 0;
  unsigned banksRemaining = 0;
  std::array<unsigned, bankCount> texturesRemaining{};
  std::array<unsigned, bankCount> lastTextures{};
  unsigned tempo = 56;
  uint32_t stepSamples = rate * 60 / (56 * 4);
  uint32_t barSamples = stepSamples * steps, barPhase = 0;
  uint64_t clock = 0;
  unsigned bar = 0;
  bool barTick = false;

  float readPos = 0, playRate = 1;
  static constexpr uint32_t crossfadeLen = 4000; // ~125 ms, avoids a click at the loop point

  float svfLow = 0, svfBand = 0;
  float swellPhase = 0, swellStep = 0;
  // Ensemble: a phase error against the shared bar, paid down a little at a
  // time. There are no notes here to drop or double, but a jumped bar clock
  // steps the swell and the transient grid at once, and that is heard as the
  // wash catching.
  int32_t gridTrim = 0;

  enum Xfade : unsigned { Steady = 0, FadingOut, FadingIn };
  unsigned xfadeState = Steady;
  static constexpr float xfadeSamples = rate * 0.18f;
  // Set once a conductor's tempo arrives: from then on the bar is the room's.
  bool following = false, xfadePending = false;
  float xfadeGain = 1;

  // Punch-in variety, one at a time, self-clearing after its window.
  unsigned punchType = PunchNone;
  uint64_t punchStartAt = 0, punchEndAt = 0;
  float punchPitchTarget = 1;
  std::array<int16_t, rate> delay{}; // 1 s, shared by Delay Throw and Smear
  unsigned delayWrite = 0, delayTapSamples = rate * 40 / 1000;
  float delayFeedback = 0, delayMix = 0, delayDamp = 0;
  float smearWobblePhase = 0, smearWobbleStep = 0;
  unsigned crushMaxHold = 1, crushHoldCounter = 0;
  float crushHeldSample = 0;
  // A short comb + allpass diffuser for Reverb, the same shape as Rill
  // Drums' room send, just fully off except during the punch window.
  std::array<float, 1601> room{};
  unsigned roomIndex = 0;
  float roomDamping = 0;
  std::array<float, 233> diffuser{};
  unsigned diffuserIndex = 0;
  float reverbMix = 0;

  float outputRamp = 0, target = 1, level = 0;
  float dcIn = 0, dcOut = 0;

  uint32_t random() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
  float unit() { return float(random() >> 8) / 16777216.0f; }
  uint32_t punchRandom() { punchRng ^= punchRng << 13; punchRng ^= punchRng >> 17; punchRng ^= punchRng << 5; return punchRng; }
  float punchUnit() { return float(punchRandom() >> 8) / 16777216.0f; }
  static float svfCoeff(float hz) {
    float clamped = std::min(hz, rate * 0.2f);
    return 2.0f * std::sin(pi * clamped / rate);
  }

  // Stretch the short cave recordings and lower their drips by default.
  // Punch pitch ratios remain relative to this bank's resting speed.
  float basePlaybackRate() const { return bank == BankCave ? 0.65f : 1.0f; }
  const Character& active_() const { return characters()[texture]; }
  static const std::array<Character, textureCount>& characters() {
    // Narrower ranges and slower swells than the first pass, which was
    // called too fast and too much the predominant thing being heard.
    // Q kept low so the sweep colors the recording rather than resonating.
    // Floors kept at or above ~700 Hz: this speaker (confirmed by Rill
    // Drums' own measurements, see its NOTES.md) barely reproduces
    // anything lower, so a sweep that dips below that doesn't read as
    // "dark," it reads as silence.
    static const std::array<Character, textureCount> table{{
      {1000, 3200, 0.35f, 0.9f, 12},  // Rain (umbrella): steady wash
      {1000, 3200, 0.4f, 0.95f, 10},  // Rain on puddle: percussive drops
      {1000, 3200, 0.4f, 0.95f, 10},  // Rain on concrete: percussive drops
      {1000, 3200, 0.4f, 0.9f, 11},   // Rain on terrace: big storm drops
      {1200, 3600, 0.45f, 0.9f, 9},   // Rain on tarpaulin: brighter, plasticky
      {1200, 4200, 0.35f, 0.85f, 12}, // Bird forest ambience: steady bed, chirps laced through
      {1200, 3800, 0.35f, 0.7f, 12},  // Birds waking: dawn chorus -- toned down, ran busy at full brightness/gain
      {1100, 3800, 0.35f, 0.85f, 13}, // Evening birds: calmer, built to loop
      {1300, 3800, 0.4f, 2.4f, 10},   // Nocturnal insects + wind: raised again -- still read as quiet next to the crickets
      {1100, 3800, 0.35f, 0.85f, 12}, // Crickets + frogs meadow: fuller mix
      {1300, 4400, 0.4f, 0.95f, 10},  // Field cricket, close: high-pitched plus a lower mechanical noise
      {1300, 4200, 0.4f, 1.7f, 9},    // Night grasshopper: raised -- also read as quiet next to the crickets
      {900, 3200, 0.35f, 0.9f, 13},   // Sea waves (Atlantic shore): steady rolling wash
      {900, 3000, 0.35f, 0.9f, 12},   // Sea waves (moderate, swirls): a touch darker
      {900, 2800, 0.4f, 0.9f, 12},    // Underwater hydrophone (waterfall): muffled, continuous rushing texture
      {900, 3000, 0.45f, 2.0f, 11},   // Cave #1: composited drips + reverb, raised gain -- sharp/quiet dynamic range
      {900, 2900, 0.45f, 2.0f, 13},   // Cave #2: same idea, a touch darker and slower
    }};
    return table;
  }

  static const std::array<BankRange, bankCount>& bankRanges() {
    static const std::array<BankRange, bankCount> table{{
      {Rain, 5},        // all five rain clips
      {BirdForest, 3},  // all three bird clips
      {InsectNight, 4}, // all four insect clips
      {OceanWaves1, 3}, // all three ocean clips
      {CaveOne, 2},     // both cave clips
    }};
    return table;
  }

  // Short taps keep recognizable snippets from recurring as miniature
  // sample loops. Delay Throw retains strong feedback; Smear is gentler.
  // Birds, Insects and Cave retain stronger, wobblier coloration.
  struct PunchStyle {
    float delayThrowFeedback, delayThrowMix;
    float smearFeedback, smearMix, smearWobbleAmp;
    unsigned smearTapMinMs, smearTapRangeMs;
    float pitchLow, pitchRange;
  };
  static const std::array<PunchStyle, bankCount>& punchStyles() {
    static const std::array<PunchStyle, bankCount> table{{
      {0.70f, 0.62f, 0.25f, 0.75f, 55.0f, 25, 21, 0.82f, 0.32f},   // Rain: 25-45 ms smear
      {0.85f, 0.80f, 0.35f, 0.96f, 140.0f, 30, 31, 0.65f, 0.55f}, // Birds: 30-60 ms smear
      {0.85f, 0.80f, 0.35f, 0.96f, 140.0f, 30, 31, 0.65f, 0.55f}, // Insects
      {0.80f, 0.68f, 0.30f, 0.85f, 95.0f, 30, 26, 0.72f, 0.45f},  // Ocean: 30-55 ms smear
      {0.85f, 0.80f, 0.35f, 0.96f, 140.0f, 30, 31, 0.72f, 0.45f}, // Cave
    }};
    return table;
  }

  // Draw without replacement; refill only after every choice was visited.
  // At a refill, avoid repeating the last choice across the bag boundary.
  unsigned pickFromBag(unsigned& remaining, unsigned count, unsigned previous) {
    if (!remaining) remaining = (1u << count) - 1;
    unsigned eligible = remaining;
    if (count > 1 && previous < count) eligible &= ~(1u << previous);
    unsigned choices = 0;
    for (unsigned i = 0; i < count; ++i) if (eligible & (1u << i)) ++choices;
    unsigned pick = random() % choices;
    for (unsigned i = 0; i < count; ++i) {
      if (!(eligible & (1u << i))) continue;
      if (pick-- == 0) { remaining &= ~(1u << i); return i; }
    }
    return 0;
  }

  unsigned pickTextureInBank(unsigned b) {
    const BankRange& r = bankRanges()[b];
    unsigned next = pickFromBag(texturesRemaining[b], r.count, lastTextures[b]);
    lastTextures[b] = next;
    return r.first + next;
  }

  void applyCharacter() {
    const unsigned chosen = 56 + random() % 37; // 56-92 BPM, overlapping Rill's and Rill Drums' ranges
    punchRng = (rng ^ 0xc2b2ae35u) | 1u;
    // In an ensemble the tempo and the bar belong to the room: a new texture
    // takes them over rather than starting a clock of its own, which put the
    // device off the shared bar on every tap and shake.
    if (!following) {
      tempo = chosen;
      stepSamples = rate * 60 / (tempo * 4);
      barSamples = stepSamples * steps;
      barPhase = 0; bar = 0;
    }
    swellStep = 2 * pi / (active_().swellBars * barSamples);
    swellPhase = unit() * 2 * pi;
    svfLow = svfBand = 0;
    readPos = 0; playRate = basePlaybackRate();
    punchType = PunchNone; punchPitchTarget = 1;
    delayFeedback = delayMix = delayDamp = 0;
    crushHoldCounter = 0;
    reverbMix = roomDamping = 0;
    room.fill(0); diffuser.fill(0);
    smearWobblePhase = 0;
  }

  void beginXfade() { xfadeState = FadingOut; }
  // In an ensemble a change waits for the bar: the fade out starts so the new
  // texture comes in on the next bar line. Alone, it starts at once.
  void requestXfade() {
    if (following) xfadePending = true;
    else beginXfade();
  }

  void stepClock() {
    if (gridTrim && barSamples) {
      int32_t bite = std::max(int32_t(-32), std::min(int32_t(32), gridTrim));
      // One counter carries the whole clock here: the step grid is read off
      // the bar phase rather than kept beside it, so moving this moves both.
      // Wrapped rather than clamped, since an unsigned counter trimmed below
      // zero lands just under the bar length and fires a bar line at once.
      int64_t moved = (int64_t(barPhase) + bite) % int64_t(barSamples);
      if (moved < 0) moved += barSamples;
      barPhase = uint32_t(moved);
      gridTrim -= bite;
    }
    if (xfadePending && xfadeState == Steady && barSamples - barPhase <= xfadeSamples) {
      xfadePending = false;
      beginXfade();
    }
    if (++barPhase >= barSamples) { barPhase = 0; ++bar; barTick = true; maybePunch(); }
  }

  void maybePunch() {
    // Raised from 0.30: delay/smear are only 2 of 5 punch types, and at the
    // original rate a listener could easily go a while without ever
    // catching one.
    if (punchType != PunchNone || punchUnit() > 0.45f) return;
    punchType = 1 + punchRandom() % (punchCount - 1);
    punchStartAt = clock;
    punchEndAt = clock + uint64_t(stepSamples) * steps * (1 + punchRandom() % 2); // one or two bars
    const PunchStyle& ps = punchStyles()[bank];
    switch (punchType) {
      case PunchPitchWobble:
        punchPitchTarget = ps.pitchLow + punchUnit() * ps.pitchRange;
        break;
      case PunchDelayThrow:
        // Milliseconds, independent of tempo: a short 40-75 ms accent.
        delayTapSamples = rate * (40 + punchRandom() % 36) / 1000;
        break;
      case PunchCrush:
        crushMaxHold = 3 + punchRandom() % 5; // light: 3-7 samples held at the deepest point
        crushHoldCounter = 0;
        break;
      case PunchSmear:
        delayTapSamples = rate * (ps.smearTapMinMs + punchRandom() % ps.smearTapRangeMs) / 1000;
        smearWobbleStep = 2 * pi * (0.1f + punchUnit() * 0.15f) / rate; // slow, ~0.1-0.25 Hz
        break;
      default: break;
    }
  }

  float punchProgress() const {
    if (punchEndAt <= punchStartAt) return 1;
    return std::min(1.0f, float(clock - punchStartAt) / float(punchEndAt - punchStartAt));
  }

  float readSample() {
    const samples::Clip& clip = samples::textures[texture];
    uint32_t length = clip.length;
    // texture switches immediately on newVariation() while the crossfade
    // still plays out the old clip's tail, so readPos may briefly belong to
    // a differently-sized clip; guard rather than assume the lengths match.
    if (readPos >= length) readPos = std::fmod(readPos, float(length));
    uint32_t i0 = uint32_t(readPos);
    uint32_t i1 = (i0 + 1 < length) ? i0 + 1 : 0;
    float frac = readPos - i0;
    float value = clip.data[i0] / 32768.0f + (clip.data[i1] / 32768.0f - clip.data[i0] / 32768.0f) * frac;
    uint32_t tailStart = length - crossfadeLen;
    if (i0 >= tailStart) {
      float t = std::min(1.0f, (readPos - tailStart) / crossfadeLen);
      uint32_t h0 = i0 - tailStart;
      uint32_t h1 = h0 + 1 < crossfadeLen ? h0 + 1 : h0;
      float head = clip.data[h0] / 32768.0f + (clip.data[h1] / 32768.0f - clip.data[h0] / 32768.0f) * frac;
      value = value * (1 - t) + head * t;
    }
    readPos += playRate;
    if (readPos >= length) readPos -= length;
    return value;
  }

 public:
  explicit Engine(uint32_t value = 0x6669656c) { seed(value); }
  void seed(uint32_t value) {
    rng = value ? value : 1;
    for (unsigned i = 0; i < 6; ++i) random(); // avoid small-seed correlation on the first texture pick
    generation = 0;
    banksRemaining = ((1u << bankCount) - 1) & ~(1u << bank);
    texturesRemaining.fill(0); lastTextures.fill(textureCount);
    generate();
  }
  void generate() {
    texture = pickTextureInBank(bank);
    ++generation;
    if (generation == 1) applyCharacter(); else requestXfade();
  }
  void newVariation() { generate(); }
  // Tap visits every bank before starting another shuffled round.
  void newBank() {
    if (bankCount <= 1) return;
    bank = pickFromBag(banksRemaining, bankCount, bank);
    texture = pickTextureInBank(bank);
    ++generation;
    requestXfade();
  }
  void setPlaying(bool playing) { target = playing ? 1.0f : 0.0f; }

  unsigned variation() const { return generation; }
  unsigned bpm() const { return tempo; }
  // Take a conductor's tempo. The swell is measured in bars, so its rate
  // follows the tempo rather than being reset by it.
  void followTempo(unsigned bpm) {
    following = bpm != 0;
    if (!bpm || bpm == tempo) return;
    tempo = std::max(40u, std::min(160u, bpm));
    stepSamples = rate * 60 / (tempo * 4);
    barSamples = stepSamples * steps;
    swellStep = 2 * pi / (active_().swellBars * barSamples);
    // The ensemble changes tempo on a bar line and this arrives within a beat
    // of it, so the phase is already right: it counts time since that line.
    if (barPhase >= barSamples) barPhase = 0;
  }
  void trimGrid(int32_t samples) { gridTrim = samples; }
  uint32_t barPhaseSamples() const { return barPhase; }
  uint32_t barSpan() const { return barSamples; }
  // The bar clock alone is not what an ensemble hears from a noise wash. Its
  // swell is: a breath several bars long, and the one gesture in this
  // instrument big enough to read across a room. Aligned to the shared bar
  // count, every device brightens and darkens together, which is the whole
  // point of putting a texture in an ensemble rather than beside one.
  void alignSwell(uint32_t sharedBar) {
    float bars = active_().swellBars;
    if (bars <= 0 || barSamples == 0) return;
    float place = std::fmod(float(sharedBar) + float(barPhase) / float(barSamples), bars) / bars;
    float error = place * 2 * pi - swellPhase;
    while (error > pi) error -= 2 * pi;
    while (error < -pi) error += 2 * pi;
    // Slowly: this is a gesture measured in bars, and hurrying it is exactly
    // the audible swoop it is meant to avoid.
    swellPhase += error * 0.015f;
  }
  unsigned currentTexture() const { return texture; }
  unsigned currentBank() const { return bank; }
  unsigned barCount() const { return bar; }
  unsigned currentPunch() const { return punchType; }
  uint32_t displayInfo() const { return (generation << 12) | (texture << 7) | tempo; }
  bool drainBarTick() { bool t = barTick; barTick = false; return t; }

  float sample() {
    if (xfadeState == FadingOut) {
      xfadeGain -= 1.0f / xfadeSamples;
      if (xfadeGain <= 0) { xfadeGain = 0; applyCharacter(); xfadeState = FadingIn; }
    } else if (xfadeState == FadingIn) {
      xfadeGain += 1.0f / xfadeSamples;
      if (xfadeGain >= 1) { xfadeGain = 1; xfadeState = Steady; }
    }
    stepClock(); ++clock;
    const Character& c = active_();

    float targetRate = basePlaybackRate() * (punchType == PunchPitchWobble ? punchPitchTarget : 1.0f);
    playRate += (targetRate - playRate) / (rate * 0.15f);
    swellPhase += swellStep; if (swellPhase > 2 * pi) swellPhase -= 2 * pi;
    float swell = 0.5f + 0.5f * std::sin(swellPhase);
    float cutoff = c.cutoffLowHz + (c.cutoffHighHz - c.cutoffLowHz) * swell;
    float f = svfCoeff(cutoff);
    float input = readSample();
    float notch = input - c.q * svfBand;
    svfLow += f * svfBand;
    float high = notch - svfLow;
    svfBand += f * high;
    float filtered = svfLow * c.gain;

    bool smear = punchType == PunchSmear;
    const PunchStyle& ps = punchStyles()[bank];
    float delayProgress = (punchType == PunchDelayThrow || smear) ? std::sin(punchProgress() * pi) : 0;
    delayFeedback += ((smear ? ps.smearFeedback : ps.delayThrowFeedback) * delayProgress - delayFeedback) / (rate * 0.05f);
    delayMix += ((smear ? ps.smearMix : ps.delayThrowMix) * delayProgress - delayMix) / (rate * 0.05f);
    // Smear wobbles its tap length and darkens each repeat, so the echoes
    // blur into the bed instead of reading as a discrete, clean echo.
    smearWobblePhase += smearWobbleStep; if (smearWobblePhase > 2 * pi) smearWobblePhase -= 2 * pi;
    float wobbleSamples = smear ? ps.smearWobbleAmp * delayProgress : 0.0f;
    unsigned tapNow = unsigned(std::max(1.0f, delayTapSamples + std::sin(smearWobblePhase) * wobbleSamples));
    unsigned readIndex = unsigned((delayWrite + delay.size() - std::min<unsigned>(delay.size() - 1, tapNow)) % delay.size());
    float delayed = delay[readIndex] / 32768.0f;
    delayDamp += (smear ? 0.5f : 0.15f) * (delayed - delayDamp);
    float delayedTone = smear ? delayDamp : delayed;
    float delayWriteValue = filtered + delayedTone * delayFeedback;
    delay[delayWrite] = int16_t(std::max(-0.98f, std::min(0.98f, delayWriteValue)) * 32767);
    if (++delayWrite == delay.size()) delayWrite = 0;
    float withDelay = filtered + delayedTone * delayMix;

    // Sample-rate crush: holds the output for a slowly swept number of
    // samples (1 at rest, up to crushMaxHold at the peak of the window and
    // back), rather than a fixed crush depth snapping on and off.
    float crushed = withDelay;
    if (punchType == PunchCrush) {
      float depth = std::sin(punchProgress() * pi);
      unsigned holdN = 1 + unsigned(depth * crushMaxHold);
      if (crushHoldCounter == 0) crushHeldSample = withDelay;
      crushHoldCounter = (crushHoldCounter + 1) % std::max(1u, holdN);
      crushed = crushHeldSample;
    } else {
      crushHoldCounter = 0;
    }

    // Reverb: a short comb + allpass diffuser, silent except during its
    // punch window, where it fades in and back out with the signal.
    float reverbTarget = punchType == PunchReverb ? 0.35f * std::sin(punchProgress() * pi) : 0.0f;
    reverbMix += (reverbTarget - reverbMix) / (rate * 0.05f);
    float delayedRoom = room[roomIndex % room.size()];
    roomDamping += 0.30f * (delayedRoom - roomDamping);
    room[roomIndex % room.size()] = crushed * 0.5f + roomDamping * 0.35f;
    ++roomIndex;
    float diffA = diffuser[diffuserIndex];
    diffuser[diffuserIndex] = delayedRoom + diffA * 0.5f;
    float wet = diffA - diffuser[diffuserIndex] * 0.5f;
    if (++diffuserIndex == diffuser.size()) diffuserIndex = 0;
    float withReverb = crushed + wet * reverbMix;

    if (punchType != PunchNone && clock >= punchEndAt) {
      punchType = PunchNone; punchPitchTarget = 1;
    }

    float mix = withReverb * xfadeGain;
    float clean = mix - dcIn + 0.999f * dcOut;
    dcIn = mix; dcOut = clean;
    level += (target - level) / (rate * 0.2f);
    outputRamp = std::min(1.0f, outputRamp + 1.0f / (rate * 0.08f));
    float x = clean * level * outputRamp;
    return x / (1 + std::abs(x));
  }
  void render(int16_t* output, unsigned count) {
    for (unsigned i = 0; i < count; ++i) output[i] = int16_t(sample() * 32767);
  }
};
}
