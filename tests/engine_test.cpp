// Copyright (c) 2026 Bruce Blay
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/Field.h"
#include <cassert>
#include <iostream>
#include <memory>

int main() {
  unsigned texturesSeen = 0;
  uint32_t ticksTotal = 0;
  for (uint32_t seed : {1u, 0x6669656cu, 0xffffffffu}) {
    auto engine = std::unique_ptr<field::Engine>(new field::Engine(seed));
    float previous = 0, peak = 0, jump = 0;
    double energy = 0;
    for (unsigned i = 0; i < field::rate * 300; ++i) {
      // Cycle textures a few times over the run, including a quick repeated tap.
      if (i && i % (field::rate * 11) == 0) engine->newVariation();
      if (i % (field::rate * 11) == field::rate / 10) engine->newVariation();
      float s = engine->sample();
      if (engine->drainBarTick()) ++ticksTotal;
      assert(std::isfinite(s) && std::abs(s) < 0.95f);
      assert(engine->bpm() >= 56 && engine->bpm() <= 92);
      assert(engine->currentTexture() < field::textureCount);
      texturesSeen |= 1u << engine->currentTexture();
      peak = std::max(peak, std::abs(s));
      jump = std::max(jump, std::abs(s - previous));
      previous = s; energy += s * s;
    }
    assert(peak > 0.02f && jump < 1.3f);
    assert(std::sqrt(energy / (field::rate * 300)) > 0.002);
    engine->setPlaying(false);
    for (unsigned i = 0; i < field::rate * 8; ++i) previous = engine->sample();
    assert(std::abs(previous) < 0.0001f);
    engine->setPlaying(true);
    energy = 0;
    for (unsigned i = 0; i < field::rate * 8; ++i) { float s = engine->sample(); energy += s * s; }
    assert(energy > 0.0005);
    std::cout << "seed " << seed << ": five-minute stability, headroom, fade/resume passed; peak=" << peak << " jump=" << jump << '\n';
  }
  assert(texturesSeen == 0x1fu); // All five rain-bank textures appeared; newVariation() never crosses a bank.
  assert(ticksTotal > 5); // The bar clock is actually advancing, not stuck.

  // Every complete bank round and per-bank clip round covers all choices.
  for (uint32_t seed : {1u, 23u, 999u}) {
    field::Engine engine(seed);
    unsigned seen = 1u << engine.currentBank();
    for (unsigned i = 1; i < field::bankCount; ++i) {
      engine.newBank();
      assert(!(seen & (1u << engine.currentBank())));
      seen |= 1u << engine.currentBank();
    }
    for (unsigned round = 0; round < 20; ++round) {
      seen = 0;
      for (unsigned i = 0; i < field::bankCount; ++i) {
        unsigned previous = engine.currentBank();
        engine.newBank();
        assert(engine.currentBank() != previous);
        assert(!(seen & (1u << engine.currentBank())));
        seen |= 1u << engine.currentBank();
      }
    }
    for (unsigned bank = 0; bank < field::bankCount; ++bank) {
      while (engine.currentBank() != bank) engine.newBank();
      engine.seed(seed); // fresh clip bag in the selected bank
      const unsigned counts[] = {5, 3, 4, 3, 2};
      unsigned previous = field::textureCount;
      for (unsigned round = 0; round < 10; ++round) {
        seen = 0;
        for (unsigned i = 0; i < counts[bank]; ++i) {
          if (round || i) engine.newVariation();
          unsigned clip = engine.currentTexture();
          assert(clip != previous && !(seen & (1u << clip)));
          assert(engine.currentBank() == bank);
          seen |= 1u << clip; previous = clip;
        }
      }
    }
  }
  std::cout << "Shuffled banks and recordings cover every choice without adjacent repeats\n";

  auto a = std::unique_ptr<field::Engine>(new field::Engine(42));
  auto b = std::unique_ptr<field::Engine>(new field::Engine(42));
  int16_t block[512];
  for (unsigned i = 0; i < 300; ++i) {
    a->render(block, 512);
    for (auto s : block) assert(s == int16_t(b->sample() * 32767));
  }
  std::cout << "Seed reproducibility and block rendering passed\n";

  auto c = std::unique_ptr<field::Engine>(new field::Engine(7));
  unsigned firstTexture = c->currentTexture();
  c->newVariation();
  assert(c->variation() == 2 && c->currentTexture() != firstTexture);
  std::cout << "New variation advances the generation and always changes texture\n";

  // Looping past a clip's end must not click: the crossfade keeps the
  // sample-to-sample jump bounded across the wrap, the same bound checked
  // above over the full five-minute run, but isolated here at the seam.
  for (uint32_t seed : {2u, 9u, 123u}) {
    field::Engine engine(seed);
    float previous = 0, jump = 0;
    for (unsigned i = 0; i < field::rate * 9; ++i) { // longer than any embedded clip
      float s = engine.sample();
      jump = std::max(jump, std::abs(s - previous));
      previous = s;
    }
    assert(jump < 1.3f);
  }
  std::cout << "Loop crossfade stays within the jump bound\n";

  // Each bank's punch style (Birds and Insects run bigger than Rain's
  // original tuning; Body is deliberately kept conservative) must still
  // stay within headroom and the jump bound.
  for (unsigned bankIndex = 0; bankIndex < field::bankCount; ++bankIndex) {
    for (uint32_t seed : {1u, 2u, 3u, 4u}) {
      field::Engine engine(seed);
      while (engine.currentBank() != bankIndex) engine.newBank();
      float previous = 0, peak = 0, jump = 0;
      for (unsigned i = 0; i < field::rate * 120; ++i) {
        if (i && i % (field::rate * 15) == 0) engine.newVariation();
        float s = engine.sample();
        assert(std::isfinite(s) && std::abs(s) < 0.95f);
        peak = std::max(peak, std::abs(s));
        jump = std::max(jump, std::abs(s - previous));
        previous = s;
      }
      assert(peak > 0.02f && jump < 1.3f);
    }
  }
  std::cout << "Every bank's punch style stays within headroom and the jump bound\n";

  // Tap (newBank()) is the only thing that crosses a bank boundary; shake
  // (newVariation()) must never leave the current bank on its own. Bank
  // texture ranges below mirror Field.h's Texture enum order.
  {
    struct Range { unsigned lo, hi; }; // [lo, hi)
    static const Range ranges[field::bankCount] = {
      {field::Rain, field::BirdForest},
      {field::BirdForest, field::InsectNight},
      {field::InsectNight, field::OceanWaves1},
      {field::OceanWaves1, field::CaveOne},
      {field::CaveOne, field::textureCount},
    };
    field::Engine engine(3);
    assert(engine.currentBank() == field::BankRain);
    for (unsigned i = 0; i < 20; ++i) {
      engine.newVariation();
      assert(engine.currentBank() == field::BankRain);
      assert(engine.currentTexture() < ranges[field::BankRain].hi);
    }
    unsigned banksSeen = 1u << engine.currentBank();
    unsigned textureBits = 0;
    for (unsigned i = 0; i < 400; ++i) {
      engine.newBank();
      unsigned b = engine.currentBank(), t = engine.currentTexture();
      banksSeen |= 1u << b;
      textureBits |= 1u << t;
      assert(t >= ranges[b].lo && t < ranges[b].hi);
    }
    assert(banksSeen == (1u << field::bankCount) - 1); // every bank visited
    assert(textureBits == (1u << field::textureCount) - 1); // every texture reachable
    std::cout << "Tap crosses banks, shake stays within one; all " << field::bankCount
              << " banks and " << field::textureCount << " textures reachable\n";
  }
}
