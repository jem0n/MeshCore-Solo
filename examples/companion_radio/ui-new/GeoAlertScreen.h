#pragma once
// Geo-alert config tool. Tools › Geo Alert.
// A single geofence whose target is either a saved waypoint (a place) or a
// person — a favourite/contact or a live [LOC] sender, keyed by pubkey prefix.
// When armed the device beeps / alerts as it crosses into (arrive/near) or out
// of (leave/away) the radius. A waypoint target is snapshotted (coord + label);
// a person target follows their latest shared position. The crossing engine
// lives in UITask::evaluateGeoAlert(). The Target row's Enter opens a picker
// (favourites first, then active live senders, then waypoints); LEFT/RIGHT
// quick-cycles the same set.
// Included by UITask.cpp after LiveShareScreen.h.

#include "../NodePrefs.h"
#include "../Waypoint.h"
#include "../LiveTrack.h"
#include "icons.h"   // drawList (shared scrolling-list helper)

class GeoAlertScreen : public UIScreen {
  UITask*    _task;
  NodePrefs* _prefs;
  bool       _dirty  = false;
  int        _sel    = 0;
  int        _scroll = 0;

  // Target picker (Enter on the Target row): a flat selectable list of people
  // and places, rebuilt from favourites / live senders / waypoints on open.
  bool       _picking = false;
  int        _pick_sel = 0, _pick_scroll = 0;
  struct Target { uint8_t kind; int32_t lat, lon; uint8_t key[6]; char name[20]; };  // kind 0=waypoint 1=person
  static const int TARGET_MAX = 24;
  Target _targets[TARGET_MAX];
  int    _target_n = 0;

  enum Kind : uint8_t { K_ENABLE, K_TARGET, K_RADIUS, K_MODE, K_BEEPER };
  struct Row { Kind kind; const char* label; };
  static const int ROW_COUNT = 5;
  static Row rows(int i) {
    static const Row R[ROW_COUNT] = {
      { K_ENABLE, "Alert" },
      { K_TARGET, "Target" },
      { K_RADIUS, "Radius" },
      { K_MODE,   "Mode" },
      { K_BEEPER, "Beeper" },
    };
    return R[i];
  }

public:
  GeoAlertScreen(UITask* task, NodePrefs* prefs) : _task(task), _prefs(prefs) {}

  void enter() { _dirty = false; _sel = 0; _scroll = 0; _picking = false; }

  void valueLabel(Kind k, char* buf, int n) {
    switch (k) {
      case K_ENABLE:
        snprintf(buf, n, "%s", (_prefs && _prefs->geo_alert_enabled) ? "ON" : "OFF");
        break;
      case K_TARGET:
        if (_prefs && _prefs->geo_alert_has_target) {
          const char* nm = _prefs->geo_alert_label[0] ? _prefs->geo_alert_label : "(unnamed)";
          // '@' prefix marks a live contact target (a moving person) vs a waypoint.
          if (_prefs->geo_alert_target_kind == 1) snprintf(buf, n, "@%s", nm);
          else                                    snprintf(buf, n, "%s", nm);
        } else {
          snprintf(buf, n, "none");
        }
        break;
      case K_RADIUS: {
        uint16_t r = NodePrefs::geoAlertRadiusMeters(_prefs ? _prefs->geo_alert_radius_idx : 1);
        if (r < 1000) snprintf(buf, n, "%um", (unsigned)r);
        else          snprintf(buf, n, "%.1fkm", r / 1000.0f);
        break;
      }
      case K_MODE:
        snprintf(buf, n, "%s", NodePrefs::geoAlertModeLabel(_prefs ? _prefs->geo_alert_mode : 0));
        break;
      case K_BEEPER:
        snprintf(buf, n, "%s", (_prefs && _prefs->geo_alert_beeper) ? "ON" : "OFF");
        break;
      default: buf[0] = '\0';
    }
  }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);
    if (_picking) { renderPicker(display); return 400; }
    display.drawCenteredHeader("GEO ALERT");

    const int valx = display.width() / 2 + 6;
    drawList(display, ROW_COUNT, _sel, _scroll, [&](int i, int y, bool sel, int reserve) {
      Row r = rows(i);
      display.drawSelectionRow(0, y - 1, display.width() - reserve, display.lineStep() - 1, sel);
      display.setCursor(4, y);
      display.print(r.label);
      char val[24];
      valueLabel(r.kind, val, sizeof(val));
      if (val[0]) display.drawTextEllipsized(valx, y, display.width() - valx - reserve, val);
    });
    return 500;
  }

  void moveSel(int dir) { _sel = (_sel + dir + ROW_COUNT) % ROW_COUNT; }

  void activate(int dir) {
    if (!_prefs) return;
    switch (rows(_sel).kind) {
      case K_ENABLE:
        _prefs->geo_alert_enabled ^= 1;
        if (_prefs->geo_alert_enabled && !_prefs->geo_alert_has_target)
          _task->showAlert("Pick a target", 1200);
        _dirty = true;
        break;
      case K_TARGET:
        cycleTarget(dir);
        break;
      case K_RADIUS:
        _prefs->geo_alert_radius_idx = (uint8_t)((_prefs->geo_alert_radius_idx
            + (dir >= 0 ? 1 : NodePrefs::GEO_ALERT_RADIUS_COUNT - 1)) % NodePrefs::GEO_ALERT_RADIUS_COUNT);
        _dirty = true;
        break;
      case K_MODE:
        _prefs->geo_alert_mode = (uint8_t)((_prefs->geo_alert_mode
            + (dir >= 0 ? 1 : NodePrefs::GEO_ALERT_MODE_COUNT - 1)) % NodePrefs::GEO_ALERT_MODE_COUNT);
        _dirty = true;
        break;
      case K_BEEPER:
        _prefs->geo_alert_beeper ^= 1;
        _dirty = true;
        break;
    }
    // Re-seed the crossing engine after any change so editing the target/radius
    // while armed can't fire a stale arrive/leave before the screen is closed.
    _task->resetGeoAlert();
  }

  // Build the selectable target set into _targets: favourites first (the quick
  // path you pin ahead of time), then active verified live senders not already
  // pinned, then saved waypoints. A person is keyed by pubkey prefix so the
  // engine follows their live position even before/independent of a [LOC].
  void buildTargets() {
    _target_n = 0;
    for (int i = 0; i < NodePrefs::FAVOURITES_COUNT && _target_n < TARGET_MAX; i++) {
      const uint8_t* pre = _prefs->favourite_contacts[i];
      bool empty = true;
      for (int b = 0; b < NodePrefs::FAVOURITE_PREFIX_LEN; b++) if (pre[b]) { empty = false; break; }
      if (empty) continue;
      ContactInfo* c = the_mesh.lookupContactByPubKey(pre, NodePrefs::FAVOURITE_PREFIX_LEN);
      if (!c) continue;
      Target& t = _targets[_target_n++];
      t.kind = 1; t.lat = t.lon = 0;
      memcpy(t.key, pre, 6);
      snprintf(t.name, sizeof(t.name), "%s", c->name);
    }
    LiveTrackStore& lt = _task->liveTrack();
    uint32_t now = rtc_clock.getCurrentTime();
    for (int i = 0; i < LiveTrackStore::CAPACITY && _target_n < TARGET_MAX; i++) {
      if (!lt.isActive(i, now) || !lt.slotAt(i).verified) continue;
      const LiveTrackStore::Entry& e = lt.slotAt(i);
      bool dup = false;
      for (int j = 0; j < _target_n; j++)
        if (_targets[j].kind == 1 && memcmp(_targets[j].key, e.key, 6) == 0) { dup = true; break; }
      if (dup) continue;
      Target& t = _targets[_target_n++];
      t.kind = 1; t.lat = e.lat_1e6; t.lon = e.lon_1e6;
      memcpy(t.key, e.key, 6);
      snprintf(t.name, sizeof(t.name), "%s", e.name);
    }
    WaypointStore& wp = _task->waypoints();
    for (int i = 0; i < wp.count() && _target_n < TARGET_MAX; i++) {
      const Waypoint& w = wp.at(i);
      Target& t = _targets[_target_n++];
      t.kind = 0; t.lat = w.lat_1e6; t.lon = w.lon_1e6;
      memset(t.key, 0, 6);
      snprintf(t.name, sizeof(t.name), "%s", w.label);
    }
  }

  // Index of the currently-configured target within _targets, or -1.
  int currentTargetIndex() const {
    for (int i = 0; i < _target_n; i++) {
      if (_targets[i].kind == 1 && _prefs->geo_alert_target_kind == 1) {
        if (memcmp(_targets[i].key, _prefs->geo_alert_key, 6) == 0) return i;
      } else if (_targets[i].kind == 0 && _prefs->geo_alert_target_kind == 0) {
        if (_targets[i].lat == _prefs->geo_alert_lat_1e6 &&
            _targets[i].lon == _prefs->geo_alert_lon_1e6) return i;
      }
    }
    return -1;
  }

  void applyTarget(const Target& t) {
    _prefs->geo_alert_target_kind = t.kind;
    if (t.kind == 1) memcpy(_prefs->geo_alert_key, t.key, 6);
    _prefs->geo_alert_lat_1e6 = t.lat;
    _prefs->geo_alert_lon_1e6 = t.lon;
    snprintf(_prefs->geo_alert_label, sizeof(_prefs->geo_alert_label), "%s", t.name);
    _prefs->geo_alert_has_target = 1;
    _dirty = true;
    _task->resetGeoAlert();
  }

  // LEFT/RIGHT quick-cycle over the same set the picker shows.
  void cycleTarget(int dir) {
    buildTargets();
    if (_target_n == 0) { _task->showAlert("No favs / waypoints", 1400); return; }
    int cur = currentTargetIndex();
    int nx = (cur < 0) ? (dir >= 0 ? 0 : _target_n - 1)
                       : ((cur + (dir >= 0 ? 1 : _target_n - 1)) % _target_n);
    applyTarget(_targets[nx]);
  }

  void openPicker() {
    if (!_prefs) return;
    buildTargets();
    if (_target_n == 0) { _task->showAlert("No favs / waypoints", 1400); return; }
    int cur = currentTargetIndex();
    _pick_sel = (cur >= 0) ? cur : 0;
    _pick_scroll = 0;
    _picking = true;
  }

  void renderPicker(DisplayDriver& display) {
    display.drawCenteredHeader("PICK TARGET");
    drawList(display, _target_n, _pick_sel, _pick_scroll, [&](int i, int y, bool sel, int reserve) {
      display.drawSelectionRow(0, y - 1, display.width() - reserve, display.lineStep() - 1, sel);
      char row[28];
      // '@' marks a person; a plain name is a waypoint.
      if (_targets[i].kind == 1) snprintf(row, sizeof(row), "@%s", _targets[i].name);
      else                       snprintf(row, sizeof(row), "%s", _targets[i].name);
      display.drawTextEllipsized(2, y, display.width() - 2 - reserve, row);
    });
  }

  bool handleInput(char c) override {
    // Target picker takes all input while open.
    if (_picking) {
      if (c == KEY_UP)   { _pick_sel = (_pick_sel > 0) ? _pick_sel - 1 : _target_n - 1; return true; }
      if (c == KEY_DOWN) { _pick_sel = (_pick_sel < _target_n - 1) ? _pick_sel + 1 : 0; return true; }
      if (c == KEY_ENTER) { applyTarget(_targets[_pick_sel]); _picking = false; return true; }
      if (c == KEY_CANCEL || c == KEY_CONTEXT_MENU) { _picking = false; return true; }
      return true;
    }
    if (c == KEY_CANCEL || c == KEY_CONTEXT_MENU) {
      if (_dirty) { the_mesh.savePrefs(); _dirty = false; }   // engine re-seeded per edit
      _task->gotoToolsScreen();
      return true;
    }
    if (c == KEY_UP)   { moveSel(-1); return true; }
    if (c == KEY_DOWN) { moveSel(+1); return true; }
    if (keyIsPrev(c))  { activate(-1); return true; }
    if (keyIsNext(c))  { activate(+1); return true; }
    if (c == KEY_ENTER) {
      if (rows(_sel).kind == K_TARGET) { openPicker(); return true; }  // full list picker
      activate(+1); return true;
    }
    return false;
  }
};
