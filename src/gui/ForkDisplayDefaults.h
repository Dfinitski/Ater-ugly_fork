#pragma once

// ANAN fork (build 214, operator 08.11): the panadapter look new users get --
// the operator's own Display panel and Receive settings. The defaults of
// SpectrumWidget::loadSettings(), the Display panel's "Reset to Defaults" and
// the one-time overwrite of earlier settings (MainWindow, kMigrationKey) all
// come from here.

namespace AetherSDR::ForkDisplayDefaults {

inline constexpr float kFftFillAlpha = 0.47f;
inline constexpr const char* kFftFillColor = "#cefff1";
inline constexpr const char* kFftLineColor = "#9dff8c";
inline constexpr float kFftLineWidth = 1.5f;
inline constexpr int kWfColorGain = 63;
inline constexpr int kWfAutoBlackOffset = 54;
// Stored position; the Display panel's FFT Floor slider shows 100 - this (10).
inline constexpr int kNoiseFloorPosition = 90;
inline constexpr int kBackgroundOpacity = 74;
inline constexpr const char* kBackgroundFillColor = "#0c0b14";
// Kept per radio and pan (ClientDisplay): FFT AVG 27 with Log Avg, waterfall rate 65.
inline constexpr int kFftAverage = 27;
inline constexpr bool kFftLogAvg = true;
inline constexpr int kWfRate = 65;

// Set once the app-wide keys were overwritten; the per-radio values are
// overwritten once per "radioId#pan" listed under kRadiosKey.
inline constexpr const char* kMigrationKey = "ForkDisplayDefaultsV214";
inline constexpr const char* kRadiosKey = "ForkDisplayDefaultsV214Radios";

}  // namespace AetherSDR::ForkDisplayDefaults
