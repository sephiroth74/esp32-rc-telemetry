#include "session.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <dirent.h>
#include <esp_heap_caps.h>
#include <esp_mac.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "calibration.h"
#include "card.h"
#include "config.h"
#include "logger.h"
#include "post.h"
#include "rc_capture.h"
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
char startUtc[24];  // the session's start in UTC, once the clock is set; written again at the end
uint32_t startMs;   // millis() at the start, to date a start that came before the clock

char names[kMaxNames][40];
const char* namePtrs[kMaxNames];
geo::Point fenceBuf[kMaxFence];
char text[kMaxJson];

bool isDir(const char* path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

bool isFile(const char* path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISREG(st.st_mode);
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

// One track as lib/geo matches it: its centre, last use, fence (into fenceBuf) and start line,
// and its name from the apps. False when its track.json cannot be read.
bool loadTrack(const char* id, geo::Track& t, char (&name)[15]) {
  char path[96];
  snprintf(path, sizeof path, "%s/%s/track.json", kTracks, id);
  JsonDocument doc;
  if (!readJson(path, doc)) return false;
  t = geo::Track();
  t.id = id;
  t.centre = pointOf(doc["centre"]);
  t.lastUsed = doc["last_used"] | 0u;
  JsonArrayConst fence = doc["fence"];
  for (JsonArrayConst v : fence) {
    if (t.fenceCount == kMaxFence) break;
    fenceBuf[t.fenceCount++] = {v[0].as<int32_t>(), v[1].as<int32_t>()};
  }
  t.fence = fenceBuf;
  snprintf(name, sizeof name, "%s", doc["name"] | "");
  snprintf(path, sizeof path, "%s/%s/start_line.json", kTracks, id);
  JsonDocument line;
  if (readJson(path, line)) {
    t.hasStartLine = true;
    t.startLine = pointOf(line.as<JsonVariantConst>());
  }
  return true;
}

// How a fix matches one track: by its fence, by the radius, or not at all.
geo::Match matchOf(const char* id, geo::Point fix, geo::Track& t, char (&name)[15]) {
  if (!loadTrack(id, t, name)) return geo::Match::None;
  geo::Resolution r = geo::resolve(&t, 1, fix, settings().matchRadiusM);
  return r.index < 0 ? geo::Match::None : r.by;
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
    geo::Track t;
    char name[15];
    geo::Match by = matchOf(names[i], fix, t, name);
    if (by == geo::Match::None) continue;
    bool better = (by == geo::Match::Fence && bestBy != geo::Match::Fence) ||
                  (by == bestBy && t.lastUsed > bestUsed) || bestBy == geo::Match::None;
    if (better) {
      bestBy = by;
      bestUsed = t.lastUsed;
      snprintf(out, sizeof out, "%s", names[i]);
    }
  }
  return bestBy != geo::Match::None;
}

// The track chosen by hand, with Track › New track or Track › Choose (§7.1): resolved in place of
// the rules for as long as the fix is within its reach, until another is chosen or the device
// restarts. A session filed there makes it the one used most recently, which the rules pick anyway.
char chosen[geo::kTrackIdMax + 1] = "";

bool resolveFix(geo::Point fix, char (&out)[geo::kTrackIdMax + 1]) {
  geo::Track t;
  char name[15];
  if (chosen[0] && matchOf(chosen, fix, t, name) != geo::Match::None) {
    snprintf(out, sizeof out, "%s", chosen);
    return true;
  }
  return resolveTrack(fix, out);
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

// The session open now, in NVS: what the next boot closes if the car's switch comes first.
void markOpen(bool open) {
  Preferences nvs;
  nvs.begin("log", /*readOnly=*/false);
  if (open) {
    nvs.putString("open", dir);
    nvs.putUInt("open_id", current.id);
  } else {
    nvs.remove("open");
    nvs.remove("open_id");
  }
  nvs.end();
}

size_t readAt(void* context, size_t offset, uint8_t* buf, size_t capacity) {
  int fd = *static_cast<int*>(context);
  if (lseek(fd, offset, SEEK_SET) < 0) return 0;
  ssize_t n = read(fd, buf, capacity);
  return n > 0 ? size_t(n) : 0;
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
  // The calibration in use (§3.8): each axis's ends and neutral in µs, null for an input left out
  // of it; null altogether for a profile never calibrated.
  const rc::Calibration* cal = calibrationActive();
  if (cal) {
    JsonObject c = doc["car_profile"]["calibration"].to<JsonObject>();
    const rc::Axis* axes[2] = {&cal->steering, &cal->throttle};
    const char* names[2] = {"steering", "throttle"};
    for (uint8_t i = 0; i < 2; ++i) {
      if (rc::absent(*axes[i])) {
        c[names[i]] = nullptr;
        continue;
      }
      c[names[i]]["negative_us"] = axes[i]->negativeUs;
      c[names[i]]["neutral_us"] = axes[i]->neutralUs;
      c[names[i]]["positive_us"] = axes[i]->positiveUs;
    }
    c["aux_us"] = cal->auxUs;
  } else {
    doc["car_profile"]["calibration"] = nullptr;
  }
  doc["start"]["automatic"] = started.automatic;
  time_t now = time(nullptr);
  if (now > 1700000000 && !startUtc[0]) {  // set from the GNSS (gnss_task.cpp), not the 1970 of a boot
    // The start is now less the session's length: nothing at the start, and to the second at the
    // close of a session begun before the clock was set.
    time_t start = now - time_t((millis() - startMs) / 1000);
    strftime(startUtc, sizeof startUtc, "%Y-%m-%dT%H:%M:%SZ", gmtime(&start));
  }
  if (startUtc[0]) {
    doc["start"]["utc"] = startUtc;
  } else {
    doc["start"]["utc"] = nullptr;
  }
  doc["start"]["fix"] = started.haveFix;
  RcLive rc;  // §4.3: an input is present once it has delivered valid pulses
  rcRead(rc);
  doc["inputs"]["steering"] = rc.present[static_cast<uint8_t>(RcInput::Steering)];
  doc["inputs"]["throttle"] = rc.present[static_cast<uint8_t>(RcInput::Throttle)];
  if (footer) {
    doc["close"] = closeName(reason);
    JsonObject f = doc["footer"].to<JsonObject>();
    f["ubx_frames"] = footer->ubxFrames;
    f["records"] = footer->records;
    f["uart_overruns"] = footer->uartOverruns;
    f["uart_errors"] = footer->uartErrors;
    f["ubx_bad_checksums"] = footer->ubxBadChecksums;
    f["ring_overflow"] = footer->ringOverflow;
    f["rc_rejects_steering"] = footer->rcRejects[0];
    f["rc_rejects_throttle"] = footer->rcRejects[1];
    f["writes"] = footer->writes;
    f["slow_writes"] = footer->slowWrites;
    f["write_max_ms"] = footer->writeMaxMs;
    f["write_p99_ms"] = footer->writeP99Ms;
    f["ring_high_water"] = footer->ringHighWater;
  }
  return writeJson(path, doc);
}

}  // namespace

void sessionLookup(bool haveFix, geo::Point fix, TrackView& out) {
  out = TrackView();
  char track[geo::kTrackIdMax + 1];
  char path[112];
  if (haveFix) {  // Track › Choose's list: every track the fix is within reach of, by either rule
    size_t n = list(kTracks);
    for (size_t i = 0; i < n && out.choices < kTrackChoicesMax; ++i) {
      if (names[i][0] == '_') continue;
      geo::Track t;
      char name[15];
      if (matchOf(names[i], fix, t, name) == geo::Match::None) continue;
      snprintf(out.choiceIds[out.choices], sizeof out.choiceIds[0], "%s", names[i]);
      snprintf(out.choiceNames[out.choices], sizeof out.choiceNames[0], "%s", name[0] ? name : names[i]);
      ++out.choices;
    }
  }
  if (!haveFix) {
    snprintf(track, sizeof track, "%s", kUnassigned);
  } else if (resolveFix(fix, track)) {
    out.found = true;
    snprintf(out.id, sizeof out.id, "%s", track);
    for (uint8_t i = 0; i < out.choices; ++i) {
      if (strcmp(out.choiceIds[i], track) == 0) out.resolved = i;
    }
    snprintf(path, sizeof path, "%s/%s/track.json", kTracks, track);
    JsonDocument doc;
    if (readJson(path, doc)) snprintf(out.name, sizeof out.name, "%s", doc["name"] | "");
    snprintf(path, sizeof path, "%s/%s/start_line.json", kTracks, track);
    out.hasStartLine = isFile(path);
    snprintf(path, sizeof path, "%s/%s/geometry.rcg", kTracks, track);
    out.hasGeometry = isFile(path);
  } else {
    return;  // a session here makes a new track: none there yet, its first session_001
  }
  snprintf(path, sizeof path, "%s/%s", kTracks, track);
  size_t n = list(path);
  uint32_t number;
  for (size_t i = 0; i < n; ++i) {
    if (geo::sessionNumber(names[i], number)) ++out.sessions;
  }
  out.next = geo::nextSession(namePtrs, n);
}

void sessionChoose(const char* track) { snprintf(chosen, sizeof chosen, "%s", track); }

bool sessionNewTrack(geo::Point fix, char (&out)[geo::kTrackIdMax + 1]) {
  if (open_ || !makeDir(kTracks) || !createTrack(fix, out)) return false;
  sessionChoose(out);
  return true;
}

bool sessionBegin(const SessionStart& start, SessionInfo& out) {
  if (open_) return false;
  started = start;
  current = SessionInfo();
  startUtc[0] = '\0';
  startMs = millis();
  if (!makeDir(kTracks)) return false;
  if (start.haveFix) {
    if (!resolveFix(start.fix, current.track)) {
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
  markOpen(true);
  out = current;
  Serial.printf("session %lu: %s/%s, track %s%s\n", (unsigned long)current.id, current.track,
                strrchr(dir, '/') + 1, current.track, current.newTrack ? " (new)" : "");
  return true;
}

void sessionEnd(rcl::Close reason, bool haveFix, geo::Point fix) {
  if (!open_) return;
  open_ = false;
  markOpen(false);
  if (!logClose(reason, LOG_CLOSE_WAIT_MS)) Serial.println("session: the close is still running");
  rcl::Footer footer;
  bool haveFooter = logLastFooter(footer);
  // Begun without a fix, and one came: into its track, as §7.1 asks — on FAT a rename.
  if (strcmp(current.track, kUnassigned) == 0 && haveFix) {
    char track[geo::kTrackIdMax + 1];
    if (resolveFix(fix, track) || createTrack(fix, track)) {
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

void sessionRecover() {
  Preferences nvs;
  nvs.begin("log", /*readOnly=*/true);
  String left = nvs.getString("open", "");
  uint32_t id = nvs.getUInt("open_id", 0);
  nvs.end();
  if (left.isEmpty() || !cardMounted()) return;  // without the card, the next boot tries again
  snprintf(dir, sizeof dir, "%s", left.c_str());
  markOpen(false);  // once, whatever comes of it

  uint32_t t0 = millis();
  char path[112];
  snprintf(path, sizeof path, "%s/session.rcl", dir);
  int fd = open(path, O_RDWR);
  if (fd < 0) {
    postLogLine("session %lu: left open, but %s is gone", (unsigned long)id, path);
    return;
  }
  rcl::Tail tail;
  auto* buf = static_cast<uint8_t*>(heap_caps_malloc(LOG_RECOVER_PIECE_BYTES, MALLOC_CAP_SPIRAM));
  bool found = buf && rcl::findTail(readAt, &fd, buf, LOG_RECOVER_PIECE_BYTES,
                                    LOG_RECOVER_GAP_BYTES, tail);
  heap_caps_free(buf);
  if (!found || tail.sessionId != id) {  // another card in the slot, say
    close(fd);
    postLogLine("session %lu: left open, but %s is not it; left alone", (unsigned long)id, path);
    return;
  }
  off_t size = lseek(fd, 0, SEEK_END);
  bool cut = ftruncate(fd, tail.end) == 0;
  close(fd);
  snprintf(path, sizeof path, "%s/meta.json", dir);
  JsonDocument doc;
  bool closed = readJson(path, doc);
  if (closed) {
    doc["close"] = "unclosed";
    closed = writeJson(path, doc);
  }
  postLogLine("session %lu: left open, closed at boot: %lu records, %lu of %lu bytes kept%s%s, "
              "%lu ms",
              (unsigned long)id, (unsigned long)tail.records, (unsigned long)tail.end,
              (unsigned long)size, cut ? "" : ", not cut", closed ? "" : ", no meta.json",
              (unsigned long)(millis() - t0));
}
