#include "cfg.h"

#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>

namespace cfg {
namespace {

const char* const kLayouts[] = {"two", "five"};
const char* const kBrightness[] = {"low", "medium", "high"};
const char* const kTextSizes[] = {"small", "large"};
const uint16_t kDimSeconds[] = {15, 30, 60, 0};

template <typename T, size_t N>
constexpr uint8_t countOf(T (&)[N]) {
  return N;
}

void note(Problems& p, const char* key) {
  if (p.count < kMaxProblems) p.keys[p.count++] = key;
}

// A string that must be one of `names`: its index, or the default and a note.
template <size_t N>
void pick(JsonVariantConst v, const char* const (&names)[N], uint8_t& field, const char* key,
          Problems& p) {
  if (v.isNull()) return;
  const char* s = v.is<const char*>() ? v.as<const char*>() : nullptr;
  for (uint8_t i = 0; s && i < N; ++i) {
    if (strcmp(s, names[i]) == 0) {
      field = i;
      return;
    }
  }
  note(p, key);
}

template <typename T>
void number(JsonVariantConst v, uint32_t lo, uint32_t hi, T& field, const char* key, Problems& p) {
  if (v.isNull()) return;
  if (v.is<uint32_t>() && v.as<uint32_t>() >= lo && v.as<uint32_t>() <= hi) {
    field = static_cast<T>(v.as<uint32_t>());
    return;
  }
  note(p, key);
}

// A section must be an object; a section that is not one counts as wrong, its keys as missing.
JsonObjectConst section(JsonDocument& doc, const char* name, Problems& p) {
  JsonVariantConst v = doc[name];
  if (!v.isNull() && !v.is<JsonObjectConst>()) note(p, name);
  return v.as<JsonObjectConst>();
}

bool validName(const char* s) {
  size_t n = s ? strlen(s) : 0;
  if (n == 0 || n > kNameMax) return false;
  for (size_t i = 0; i < n; ++i) {
    if (s[i] < 0x20 || s[i] > 0x7E) return false;  // what the display's fonts can draw
  }
  return true;
}

void readProfiles(JsonVariantConst v, Config& out, Problems& p) {
  if (v.isNull()) return;
  JsonArrayConst list = v.as<JsonArrayConst>();
  if (!v.is<JsonArrayConst>() || list.size() == 0) {
    note(p, "profiles");
    return;
  }
  if (list.size() > kMaxProfiles) note(p, "profiles");  // the first eight are kept
  uint8_t n = 0;
  bool bad = false;
  for (JsonVariantConst item : list) {
    if (n == kMaxProfiles) break;
    const char* name = item["name"].is<const char*>() ? item["name"].as<const char*>() : nullptr;
    if (validName(name)) {
      snprintf(out.profiles[n].name, sizeof out.profiles[n].name, "%s", name);
    } else {
      defaultName(n, out.profiles[n].name);
      bad = true;
    }
    ++n;
  }
  out.profileCount = n;
  if (bad) note(p, "profiles[].name");
}

}  // namespace

void defaultName(uint8_t index, char (&out)[kNameMax + 1]) {
  snprintf(out, sizeof out, "Car %u", index + 1);
}

Read read(const char* json, size_t length, const Config& defaults, Config& out,
          Problems& problems) {
  out = defaults;
  problems = Problems();
  size_t i = 0;
  while (i < length && (json[i] == ' ' || json[i] == '\n' || json[i] == '\r' || json[i] == '\t'))
    ++i;
  if (i == length) return Read::Empty;

  JsonDocument doc;
  if (deserializeJson(doc, json, length) || !doc.is<JsonObjectConst>()) return Read::NotJson;

  pick(section(doc, "keys", problems)["layout"], kLayouts, out.keysLayout, "keys.layout", problems);
  JsonObjectConst display = section(doc, "display", problems);
  pick(display["brightness"], kBrightness, out.brightness, "display.brightness", problems);
  pick(display["text"], kTextSizes, out.textSize, "display.text", problems);
  JsonVariantConst dim = display["dim_after_s"];
  if (!dim.isNull()) {
    bool found = false;
    for (uint8_t k = 0; dim.is<uint32_t>() && k < countOf(kDimSeconds); ++k) {
      if (dim.as<uint32_t>() == kDimSeconds[k]) {
        out.dimDelay = k;
        found = true;
      }
    }
    if (!found) note(problems, "display.dim_after_s");
  }
  JsonObjectConst sd = section(doc, "sd", problems);
  number(sd["min_free_mb"], 16, 1u << 20, out.minFreeMb, "sd.min_free_mb", problems);
  number(sd["warn_free_mb"], 16, 1u << 20, out.warnFreeMb, "sd.warn_free_mb", problems);
  if (out.warnFreeMb < out.minFreeMb) {  // a warning level under the floor would never show
    note(problems, "sd.warn_free_mb");
    out.warnFreeMb = defaults.warnFreeMb > out.minFreeMb ? defaults.warnFreeMb : out.minFreeMb;
  }
  JsonObjectConst session = section(doc, "session", problems);
  JsonVariantConst start = session["auto_start"];
  if (!start.isNull()) {
    if (start.is<bool>()) {
      out.autoStart = start.as<bool>();
    } else {
      note(problems, "session.auto_start");
    }
  }
  number(session["auto_stop_min"], 1, 60, out.autoStopMin, "session.auto_stop_min", problems);
  number(section(doc, "track", problems)["match_radius_m"], 50, 2000, out.matchRadiusM,
         "track.match_radius_m", problems);
  readProfiles(doc["profiles"], out, problems);
  return Read::Ok;
}

size_t write(const Config& c, char* out, size_t capacity) {
  JsonDocument doc;
  doc["version"] = 1;
  doc["keys"]["layout"] = kLayouts[c.keysLayout % countOf(kLayouts)];
  doc["display"]["brightness"] = kBrightness[c.brightness % countOf(kBrightness)];
  doc["display"]["dim_after_s"] = kDimSeconds[c.dimDelay % countOf(kDimSeconds)];
  doc["display"]["text"] = kTextSizes[c.textSize % countOf(kTextSizes)];
  doc["sd"]["min_free_mb"] = c.minFreeMb;
  doc["sd"]["warn_free_mb"] = c.warnFreeMb;
  doc["session"]["auto_start"] = c.autoStart;
  doc["session"]["auto_stop_min"] = c.autoStopMin;
  doc["track"]["match_radius_m"] = c.matchRadiusM;
  JsonArray profiles = doc["profiles"].to<JsonArray>();
  for (uint8_t i = 0; i < c.profileCount && i < kMaxProfiles; ++i) {
    profiles.add<JsonObject>()["name"] = c.profiles[i].name;
  }
  size_t n = measureJsonPretty(doc);
  if (n + 1 > capacity) return 0;
  return serializeJsonPretty(doc, out, capacity);
}

}  // namespace cfg
