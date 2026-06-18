#pragma once

// Community-suggested per-region RF presets (freq/bw/sf/cr), sourced from the
// MeshCore app's "suggested_radio_settings" config plus a few community additions.
// Kept here (rather than guessed) since wrong values are a regulatory/connectivity
// concern, not just cosmetic.
struct RadioPreset {
  const char* name;
  float freq;   // MHz
  float bw;     // kHz
  uint8_t sf;
  uint8_t cr;
};

// Deprecated entries and anything outside the 868-9xx MHz bands (433MHz) have been
// dropped — only current, in-band presets are kept.
static const RadioPreset RADIO_PRESETS[] = {
  { "Australia",               915.800f, 250.0f, 10, 5 },
  { "Australia (Narrow)",      916.575f,  62.5f,  7, 8 },
  { "Australia (Mid)",         915.075f, 125.0f,  9, 5 },
  { "Australia: SA, WA",       923.125f,  62.5f,  8, 8 },
  { "Australia: QLD",          923.125f,  62.5f,  8, 5 },
  { "Brazil",                  923.125f,  62.5f,  8, 8 },
  { "EU/UK (Narrow)",          869.618f,  62.5f,  8, 8 },
  { "Czech Republic (Narrow)", 869.432f,  62.5f,  7, 5 },
  { "Netherlands",             869.618f,  62.5f,  7, 5 },
  { "New Zealand",             917.375f, 250.0f, 11, 5 },
  { "New Zealand (Narrow)",    917.375f,  62.5f,  7, 5 },
  { "Portugal 868",            869.618f,  62.5f,  7, 6 },
  { "Switzerland",             869.618f,  62.5f,  8, 8 },
  { "USA/Canada (Recommended)",910.525f,  62.5f,  7, 5 },
  { "USA/SOCAL",                927.875f,  62.5f,  7, 5 },
  { "Vietnam (Narrow)",        920.250f,  62.5f,  8, 5 },
};
static const int RADIO_PRESET_COUNT = sizeof(RADIO_PRESETS) / sizeof(RADIO_PRESETS[0]);
