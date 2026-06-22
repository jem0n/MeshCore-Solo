#pragma once
// Geo-alert config tool. Tools › Geo Alert.
// A single geofence around a saved waypoint: when armed, the device beeps and
// shows an alert as it crosses into (arrive) or out of (leave) the radius. The
// target is snapshotted from a waypoint (coord + label), so the alert keeps
// working even if that waypoint is later edited or deleted. The crossing engine
// itself lives in UITask::evaluateGeoAlert().
// Included by UITask.cpp after LiveShareScreen.h.

#include "../NodePrefs.h"
#include "../Waypoint.h"
#include "icons.h"   // drawList (shared scrolling-list helper)

class GeoAlertScreen : public UIScreen {
  UITask*    _task;
  NodePrefs* _prefs;
  bool       _dirty  = false;
  int        _sel    = 0;
  int        _scroll = 0;

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

  void enter() { _dirty = false; _sel = 0; _scroll = 0; }

  void valueLabel(Kind k, char* buf, int n) {
    switch (k) {
      case K_ENABLE:
        snprintf(buf, n, "%s", (_prefs && _prefs->geo_alert_enabled) ? "ON" : "OFF");
        break;
      case K_TARGET:
        if (_prefs && _prefs->geo_alert_has_target && _prefs->geo_alert_label[0])
          snprintf(buf, n, "%s", _prefs->geo_alert_label);
        else if (_prefs && _prefs->geo_alert_has_target)
          snprintf(buf, n, "(unnamed)");
        else
          snprintf(buf, n, "none");
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

  // Cycle the target through the saved waypoints, snapshotting the chosen one's
  // coordinate + label into prefs so the alert is independent of later edits.
  void cycleTarget(int dir) {
    WaypointStore& wp = _task->waypoints();
    int total = wp.count();
    if (total == 0) { _task->showAlert("Mark a waypoint first", 1400); return; }

    // Locate the current target among the waypoints (by coordinate match).
    int cur = -1;
    if (_prefs->geo_alert_has_target) {
      for (int i = 0; i < total; i++)
        if (wp.at(i).lat_1e6 == _prefs->geo_alert_lat_1e6 &&
            wp.at(i).lon_1e6 == _prefs->geo_alert_lon_1e6) { cur = i; break; }
    }
    int nx = (cur < 0) ? (dir >= 0 ? 0 : total - 1)
                       : ((cur + (dir >= 0 ? 1 : total - 1)) % total);

    const Waypoint& w = wp.at(nx);
    _prefs->geo_alert_lat_1e6 = w.lat_1e6;
    _prefs->geo_alert_lon_1e6 = w.lon_1e6;
    snprintf(_prefs->geo_alert_label, sizeof(_prefs->geo_alert_label), "%s", w.label);
    _prefs->geo_alert_has_target = 1;
    _dirty = true;
  }

  bool handleInput(char c) override {
    if (c == KEY_CANCEL || c == KEY_CONTEXT_MENU) {
      if (_dirty) { the_mesh.savePrefs(); _dirty = false; }   // engine re-seeded per edit
      _task->gotoToolsScreen();
      return true;
    }
    if (c == KEY_UP)   { moveSel(-1); return true; }
    if (c == KEY_DOWN) { moveSel(+1); return true; }
    if (keyIsPrev(c))  { activate(-1); return true; }
    if (keyIsNext(c))  { activate(+1); return true; }
    if (c == KEY_ENTER) { activate(+1); return true; }
    return false;
  }
};
