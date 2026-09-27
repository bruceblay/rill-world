// Copyright (c) 2026 Bruce Blay
// SPDX-License-Identifier: GPL-3.0-or-later
#include <M5Unified.h>
#include <atomic>
#include <esp_system.h>
#include "Field.h"
// The ensemble radio. ESP-NOW pulls in the WiFi stack, about 390 KB, and the
// loops already filled the chip: the rain-on-wheelbarrow loop (500 KB) was
// dropped to make room. Build with -DENSEMBLE_SYNC=0 to play alone without it.
#ifndef ENSEMBLE_SYNC
#define ENSEMBLE_SYNC 1
#endif
#if ENSEMBLE_SYNC
#include "Radio.h"
#endif
#include "Weather.h"
#include "ShakeDetector.h"

// Working title. Display and controls run separately from the audio producer.
static field::Engine engine;
static weather::Scene scene;
static ShakeDetector shake;
static bool infoVisible = false, audioFailed = false;
static uint32_t infoAt = 0, worstVisualUs = 0;
static int16_t buffers[3][512];
static std::atomic<bool> playing{true}, changeRequested{false}, repaintRequested{false}, bankChangeRequested{false};
static std::atomic<uint32_t> sceneInfo{0};
static std::atomic<bool> tickFlag{false};
static std::atomic<uint32_t> worstRenderUs{0}, queueErrors{0};
// The ensemble's tempo, the phase error against its bar, and which bar it is
// on, handed to the audio task through atomics the same way every other
// request is: this engine is not safe to touch from two tasks at once.
static std::atomic<uint32_t> ensembleTempo{0}, ensembleBar{0};
static std::atomic<int32_t> gridTrim{0};
// Starts at the quietest step of the side button's cycle: World is the
// environmental layer under an ensemble, and can be brought up from there.
static uint8_t volume = 45;

void audioTask(void*) {
  unsigned index = 0;
  for (;;) {
    if (changeRequested.exchange(false)) engine.newVariation();
    if (bankChangeRequested.exchange(false)) engine.newBank();
    engine.setPlaying(playing.load());
    if (uint32_t bpm = ensembleTempo.exchange(0)) engine.followTempo(bpm);
    if (int32_t trim = gridTrim.exchange(0)) engine.trimGrid(trim);
    if (uint32_t bar = ensembleBar.exchange(0)) engine.alignSwell(bar - 1);
    uint32_t start = micros();
    engine.render(buffers[index], 512);
    uint32_t elapsed = micros() - start;
    sceneInfo.store(engine.displayInfo());
    if (engine.drainBarTick()) tickFlag.store(true);
    if (elapsed > worstRenderUs) worstRenderUs = elapsed;
    while (!M5.Speaker.playRaw(buffers[index], 512, field::rate, false, 1, 0)) {
      ++queueErrors;
      vTaskDelay(1);
    }
    index = (index + 1) % 3;
  }
}

void motionTask(void*) {
  for (;;) {
    if (M5.Imu.isEnabled() && (M5.Imu.update() & m5::IMU_Class::sensor_mask_accel)) {
      const auto data = M5.Imu.getImuData();
      if (shake.update(data.accel.x,data.accel.y,data.accel.z,millis())) changeRequested = true;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// The tempo on the data view: the ensemble's, or the one it is about to
// change to after a hold of the side button, so the hold shows at once.
static unsigned shownTempo(unsigned own) {
#if ENSEMBLE_SYNC
  if (unsigned t = radio::tempoAhead()) return t;
#endif
  return own;
}

void draw() {
  auto& d = M5.Display;
  d.fillScreen(0x1082);
  d.setTextColor(0xD692, 0x1082);
  d.setTextSize(3);
  static const char* banks[] = {"RAIN", "BIRDS", "BUGS", "OCEAN", "CAVE"};
  d.setCursor(16, 14); d.print(banks[engine.currentBank()]);
  d.drawFastHLine(16, 48, 208, 0x4208);
  d.setTextSize(2);
  uint32_t info = sceneInfo.load();
  static const char* textures[] = {"Umbrella", "Puddle", "Concrete", "Terrace", "Tarp",
                                    "Forest", "Dawn", "Dusk",
                                    "Night", "Meadow", "Cricket", "Hopper",
                                    "Waves", "Swirls", "Underwtr",
                                    "Drip 1", "Drip 2"};
  d.setCursor(16, 57); d.printf("%s", textures[(info >> 7) & 31]);
  d.setCursor(16, 82); d.printf("%02u  %u BPM  bar %u", unsigned(info >> 12), shownTempo(unsigned(info & 127)), engine.barCount());
  d.setCursor(16, 108);
  if (playing) d.printf("Vol %u%%", unsigned(volume) * 100 / 255);
  else d.print("resting");
  int battery = M5.Power.getBatteryLevel();
  d.setCursor(130,108);
  if (battery >= 0) d.printf("Bat %d%%", std::min(100,battery));
  else d.print("Bat --");
}

void setup() {
  auto cfg = M5.config();
  cfg.internal_spk = true; cfg.internal_mic = false; cfg.internal_imu = true;
  M5.begin(cfg);
  Serial.begin(115200);
  engine.seed(esp_random());
  scene.seed(esp_random());
  sceneInfo.store(engine.displayInfo());
  M5.BtnA.setHoldThresh(650);
  M5.BtnB.setHoldThresh(650);
  M5.Display.setRotation(1);
  M5.Display.setBrightness(55);
  M5.Speaker.setVolume(volume);
  scene.render(0,false);
  M5.Display.pushImage(0,0,240,135,reinterpret_cast<const lgfx::rgb565_t*>(scene.pixels()));
  if (!M5.Speaker.begin()) {
    audioFailed = true; M5.Display.fillScreen(0x1082);
    M5.Display.setTextSize(2); M5.Display.setCursor(16, 62); M5.Display.print("audio error");
    return;
  }
  if (xTaskCreatePinnedToCore(motionTask, "field-motion", 4096, nullptr, 1, nullptr, 0) != pdPASS)
    Serial.println("Motion task unavailable");
#if ENSEMBLE_SYNC
  // World keeps the clock only when alone: a wash with no pulse of its own
  // is the worst device to hold the ensemble's tempo.
  if (!radio::begin(engine.bpm(), false, true))
    Serial.println("ensemble radio unavailable; playing alone");
#endif
  if (xTaskCreatePinnedToCore(audioTask, "field-audio", 4096, nullptr, 3, nullptr, 1) != pdPASS) {
    audioFailed = true; M5.Display.fillScreen(0x1082);
    M5.Display.setTextSize(2); M5.Display.setCursor(16, 62); M5.Display.print("audio error");
  }
}

// Keep the wash on the shared bar. Two things travel: the bar clock, which
// puts the transients on the ensemble's grid, and the bar count, which the
// swell is aligned against so every device breathes together.
#if ENSEMBLE_SYNC
void serviceEnsemble() {
  if (!radio::up()) return;
  int64_t now = esp_timer_get_time();
  radio::service(now, 4);
  static int64_t lastTrim = 0;
  if (now - lastTrim < 120000) return;
  lastTrim = now;
  ensembleTempo.store(radio::tempo());
  // Offset by one, so zero can mean nothing new to say.
  ensembleBar.store(radio::beatIndex() / 4 + 1);
  int64_t untilBar = 0, barMicros = 0;
  radio::barWindow(now, 4, untilBar, barMicros);
  if (barMicros <= 0) return;
  int64_t span = int64_t(engine.barSpan());
  if (span <= 0) return;
  int64_t want = span - (untilBar * int64_t(field::rate)) / 1000000;
  while (want < 0) want += span;
  want %= span;
  int64_t error = want - int64_t(engine.barPhaseSamples());
  error = ((error % span) + span) % span;
  if (error > span / 2) error -= span;
  // A quarter of the error at a time. A wash has no attack to hide a
  // correction behind, so it is spread thinner here than on the instruments.
  gridTrim.store(int32_t(error / 4));
}
#else
void serviceEnsemble() {}
#endif

void loop() {
  M5.update();
  serviceEnsemble();
  uint32_t now = millis();
  bool changed = false;
  // Tap moves to another environment; shake picks another recording within it.
  if (M5.BtnA.wasClicked()) { bankChangeRequested = true; repaintRequested = true; playing = true; changed = true; }
  if (M5.BtnA.wasHold()) { playing = !playing; changed = true; }
  static uint32_t lastScene = 0;
  uint32_t currentScene = sceneInfo.load();
  const bool newTexture = lastScene != 0 && (currentScene >> 12) != (lastScene >> 12);
  if (currentScene != lastScene) { lastScene = currentScene; changed = true; }
  if (M5.BtnB.wasClicked()) {
    volume = volume >= 255 ? 45 : volume + 30;
    M5.Speaker.setVolume(volume); changed = true;
    infoVisible = true; infoAt = now;
  }
  // Holding the side button slows the whole ensemble a step, from the bar
  // after next; past the slowest it comes round to the fastest.
  if (M5.BtnB.wasHold()) {
#if ENSEMBLE_SYNC
    radio::slower();
#endif
    changed = true; infoVisible = true; infoAt = now;
  }
  static uint32_t frameAt = 0;
  static unsigned lastBank = 0;
  unsigned currentBank = engine.currentBank();
  if (currentBank != lastBank) { lastBank = currentBank; scene.setBank(currentBank); }
  const bool newVisual = repaintRequested.exchange(false);
  if (newTexture || newVisual) { scene.regenerate(); infoVisible=false; frameAt=now-33; }
  if (infoVisible && uint32_t(now - infoAt) >= 4000) infoVisible = false;
  static bool wasInfoVisible = false;
  if (!audioFailed) {
    if (infoVisible) {
      if (changed || !wasInfoVisible) draw();
    } else if (wasInfoVisible || uint32_t(now - frameAt) >= 33) {
      float dt = std::min(0.25f,float(uint32_t(now-frameAt))/1000);
      frameAt = now;
      uint32_t started = micros();
      bool beat = tickFlag.exchange(false);
      scene.render(dt, beat);
      M5.Display.pushImage(0,0,240,135,reinterpret_cast<const lgfx::rgb565_t*>(scene.pixels()));
      worstVisualUs = std::max(worstVisualUs,uint32_t(micros()-started));
    }
  }
  wasInfoVisible = infoVisible;
  static uint32_t report = 0;
  if (millis() - report >= 10000) {
    report = millis();
    Serial.printf("render worst=%lu us / 16000 us; queue errors=%lu; heap=%u; generation=%lu BPM=%lu texture=%lu visual=%u visual_us=%lu\n",
      (unsigned long)worstRenderUs.load(), (unsigned long)queueErrors.load(), ESP.getFreeHeap(),
      (unsigned long)(currentScene >> 12), (unsigned long)(currentScene & 127),
      (unsigned long)((currentScene >> 7) & 31),
      scene.generation(),(unsigned long)worstVisualUs);
  }
  delay(10);
}
