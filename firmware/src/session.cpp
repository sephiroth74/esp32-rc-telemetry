#include "session.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <dirent.h>
#include <esp_mac.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "config.h"
#include "logger.h"
#include "settings.h"

namespace {

constexpr const char* kTracks = "/sdcard/tracks";
constexpr const char* kUnassigned = "_unassigned";
constexpr size_t kMaxNames = 128;  // folders in /tracks, or entries in a track's folder
constexpr size_t kMaxFence = 128;  // vertices of one fence
constexpr size_t kMaxJson = 8192;  // a track.json with its fence

SessionInfo current;
bool open_ = false;
SessionStart started;
char dir[96];  // /sdcard/tracks/<track>/session_NNN
char startUtc[24];  // the session's start, once the clock is set; written again at the end

char names[kMaxNames][40];
const char* namePtrs[kMaxNames];
geo::Point fenceBuf[kMaxFence];
char text[kMaxJson];

bool isDir(const char* path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

bool makeDir(const char* path) { return isDir(path) || mkdir(path, 0755) == 0; }

// The names in `path`, into names[]/namePtrs[]; how many.
size_t list(const char* path) {
  size_t n = 0;
  DIR* d = opendir(path);
  if (!d) return 0;
  while (dirent* e = readdir(d)) {
    if (n == kMaxNames) break;
    if (e->d_name[0] == '.') continue;
    snprintf(names[n], sizeof names[n], "%s", e->d_name);
    namePtrs[n] = names[n];
    ++n;
  }
  closedir(d);
  return n;
}

bool readJson(const char* path, JsonDocument& doc) {
  FILE* f = fopen(path, "r");
  if (!f) return false;
  size_t n = fread(text, 1, sizeof text - 1, f);
  fclose(f);
  text[n] = '\0';
  return !deserializeJson(doc, static_cast<const char*>(text), n);  // copied: text is reused
}

bool writeJson(const char* path, const JsonDocument& doc) {
  size_t n = serializeJsonPretty(doc, text, sizeof text);
  if (!n || n >= sizeof text) return false;
  FILE* f = fopen(path, "w");
  if (!f) return false;
  bool ok = fwrite(text, 1, n, f) == n;
  return fclose(f) == 0 && ok;
}

geo::Point pointOf(JsonVariantConst v) {
  return {v["lat_e7"].as<int32_t>(), v["lon_e7"].as<int32_t>()};
}

// docs/algorithms.md, "Which track": the fence first, then the radius among the unfenced, the
// most recently used of several. One track at a time against lib/geo's rules, so that no more than
// one fence is in memory: geo::resolve() on a single track says how it matches.
bool resolveTrack(geo::Point fix, char (&out)[geo::kTrackIdMax + 1]) {
  size_t n = list(kTracks);
  geo::Match bestBy = geo::Match::None;
  uint32_t bestUsed = 0;
  for (size_t i = 0; i < n; ++i) {
    if (names[i][0] == '_') continue;  // _unassigned, and anything else that is not a track
    char path[96];
    snprintf(path, sizeof path, "%s/%s/track.json", kTracks, names[i]);
    JsonDocument doc;
    if (!readJson(path, doc)) continue;
    geo::Track t;
    t.id = names[i];
    t.centre = pointOf(doc["centre"]);
    t.lastUsed = doc["last_used"] | 0u;
    JsonArrayConst fence = doc["fence"];
    t.fenceCount = 0;
    for (JsonArrayConst v : fence) {
      if (t.fenceCount == kMaxFence) break;
      fenceBuf[t.fenceCount++] = {v[0].as<int32_t>(), v[1].as<int32_t>()};
    }
    t.fence = fenceBuf;
    snprintf(path, sizeof path, "%s/%s/start_line.json", kTracks, names[i]);
    JsonDocument line;
    if (readJson(path, line)) {
      t.hasStartLine = true;
      t.startLine = pointOf(line.as<JsonVariantConst>());
    }
    geo::Resolution r = geo::resolve(&t, 1, fix, settings().matchRadiusM);
    if (r.index < 0) continue;
    bool better = (r.by == geo::Match::Fence && bestBy != geo::Match::Fence) ||
                  (r.by == bestBy && t.lastUsed > bestUsed) || bestBy == geo::Match::None;
    if (better) {
      bestBy = r.by;
      bestUsed = t.lastUsed;
      snprintf(out, sizeof out, "%s", names[i]);
    }
  }
  return bestBy != geo::Match::None;
}

// A new folder at the fix, with its track.json (docs/algorithms.md, "A new track's name").
bool createTrack(geo::Point fix, char (&out)[geo::kTrackIdMax + 1]) {
  size_t n = list(kTracks);
  if (!geo::newTrackId(fix, namePtrs, n, out)) return false;
  char path[96];
  snprintf(path, sizeof path, "%s/%s", kTracks, out);
  if (!makeDir(path)) return false;
  JsonDocument doc;
  doc["version"] = 1;
  doc["id"] = out;
  doc["name"] = "";
  doc["last_used"] = 0;
  doc["centre"]["lat_e7"] = fix.latE7;
  doc["centre"]["lon_e7"] = fix.lonE7;
  snprintf(path, sizeof path, "%s/%s/track.json", kTracks, out);
  return writeJson(path, doc);
}

// The session goes on record in its track: last_used, leaving the apps' keys as they were.
void touchTrack(const char* track, uint32_t sessionId) {
  if (track[0] == '_') return;
  char path[96];
  snprintf(path, sizeof path, "%s/%s/track.json", kTracks, track);
  JsonDocument doc;
  if (!readJson(path, doc)) return;
  doc["last_used"] = sessionId;
  writeJson(path, doc);
}

// The next session_NNN in `track`, made: its number, and dir[] its path.
uint32_t makeSessionDir(const char* track) {
  char trackDir[64];
  snprintf(trackDir, sizeof trackDir, "%s/%s", kTracks, track);
  if (!makeDir(trackDir)) return 0;
  size_t n = list(trackDir);
  uint32_t number = geo::nextSession(namePtrs, n);
  char name[geo::kSessionNameMax + 1];
  geo::sessionName(number, name);
  snprintf(dir, sizeof dir, "%s/%s", trackDir, name);
  return mkdir(dir, 0755) == 0 ? number : 0;
}

uint32_t nextSessionId() {
  Preferences nvs;
  nvs.begin("log", /*readOnly=*/false);
  uint32_t id = nvs.getUInt("session", 0) + 1;
  nvs.putUInt("session", id);
  nvs.end();
  return id;
}

const char* closeName(rcl::Close c) {
  switch (c) {
    case rcl::Close::ByHand:
      return "by_hand";
    case rcl::Close::AutoStop:
      return "auto_stop";
    case rcl::Close::PowerOff:
      return "power_off";
    case rcl::Close::CardError:
      return "card_error";
  }
  return "?";
}

void macText(char (&out)[18]) {
  uint8_t mac[6] = {};
  esp_efuse_mac_get_default(mac);
  snprintf(out, sizeof out, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3],
           mac[4], mac[5]);
}

// meta.json (§7.1): what the session is, at its start; the footer's numbers join it at the end.
bool writeMeta(const rcl::Footer* footer, rcl::Close reason) {
  char path[112];
  snprintf(path, sizeof path, "%s/meta.json", dir);
  JsonDocument doc;
  doc["version"] = 1;
  doc["session_id"] = current.id;
  doc["track"] = current.track;
  doc["number"] = current.number;
  doc["firmware"] = FW_VERSION;
  char mac[18];
  macText(mac);
  doc["device"] = mac;
  doc["post"]["failed_mask"] = started.postMask;
  doc["post"]["summary"] = started.postSummary;
  doc["car_profile"]["index"] = started.profile;
  doc["car_profile"]["name"] = started.profileName;
  doc["car_profile"]["calibration"] = nullptr;  // phase 6
  doc["start"]["automatic"] = started.automatic;
  time_t now = time(nullptr);
  if (now > 1700000000 && !footer) {  // set from the GNSS (gnss_task.cpp), not the 1970 of a boot
    char utc[24];
    strftime(utc, sizeof utc, "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
    snprintf(startUtc, sizeof startUtc, "%s", utc);
  }
  if (startUtc[0]) {
    doc["start"]["utc"] = startUtc;
  } else {
    doc["start"]["utc"] = nullptr;
  }
  doc["start"]["fix"] = started.haveFix;
  doc["inputs"]["steering"] = false;  // phase 6
  doc["inputs"]["throttle"] = false;
  if (footer) {
    doc["close"] = closeName(reason);
    JsonObject f = doc["footer"].to<JsonObject>();
    f["ubx_frames"] = footer->ubxFrames;
    f["records"] = footer->records;
    f["uart_overruns"] = footer->uartOverruns;
    f["uart_errors"] = footer->uartErrors;
    f["ubx_bad_checksums"] = footer->ubxBadChecksums;
    f["ring_overflow"] = footer->ringOverflow;
    f["writes"] = footer->writes;
    f["slow_writes"] = footer->slowWrites;
    f["write_max_ms"] = footer->writeMaxMs;
    f["write_p99_ms"] = footer->writeP99Ms;
    f["ring_high_water"] = footer->ringHighWater;
  }
  return writeJson(path, doc);
}

}  // namespace

bool sessionBegin(const SessionStart& start, SessionInfo& out) {
  if (open_) return false;
  started = start;
  current = SessionInfo();
  startUtc[0] = '\0';
  if (!makeDir(kTracks)) return false;
  if (start.haveFix) {
    if (!resolveTrack(start.fix, current.track)) {
      if (!createTrack(start.fix, current.track)) return false;
      current.newTrack = true;
    }
  } else {
    snprintf(current.track, sizeof current.track, "%s", kUnassigned);
  }
  current.number = makeSessionDir(current.track);
  if (!current.number) return false;
  current.id = nextSessionId();
  touchTrack(current.track, current.id);
  if (!writeMeta(nullptr, rcl::Close::ByHand)) return false;

  rcl::SessionHeader h;
  h.sessionId = current.id;
  esp_efuse_mac_get_default(h.device);
  h.post = start.postMask;
  snprintf(h.firmware, sizeof h.firmware, "%s", FW_VERSION);
  h.carProfile = start.profile;
  char path[112];
  snprintf(path, sizeof path, "%s/session.rcl", dir);
  if (!logOpen(path, h)) return false;
  open_ = true;
  out = current;
  Serial.printf("session %lu: %s/%s, track %s%s\n", (unsigned long)current.id, current.track,
                strrchr(dir, '/') + 1, current.track, current.newTrack ? " (new)" : "");
  return true;
}

void sessionEnd(rcl::Close reason, bool haveFix, geo::Point fix) {
  if (!open_) return;
  open_ = false;
  if (!logClose(reason, LOG_CLOSE_WAIT_MS)) Serial.println("session: the close is still running");
  rcl::Footer footer;
  bool haveFooter = logLastFooter(footer);
  // Begun without a fix, and one came: into its track, as §7.1 asks — on FAT a rename.
  if (strcmp(current.track, kUnassigned) == 0 && haveFix) {
    char track[geo::kTrackIdMax + 1];
    if (resolveTrack(fix, track) || createTrack(fix, track)) {
      char from[96];
      snprintf(from, sizeof from, "%s", dir);
      uint32_t number = makeSessionDir(track);  // dir[] now the new one, made empty
      if (number && rmdir(dir) == 0 && rename(from, dir) == 0) {
        snprintf(current.track, sizeof current.track, "%s", track);
        current.number = number;
        touchTrack(track, current.id);
        Serial.printf("session %lu: moved to %s\n", (unsigned long)current.id, dir);
      } else {
        snprintf(dir, sizeof dir, "%s", from);  // stays where it was
      }
    }
  }
  writeMeta(haveFooter ? &footer : nullptr, reason);
}

bool sessionOpen() { return open_; }

const SessionInfo& sessionCurrent() { return current; }
