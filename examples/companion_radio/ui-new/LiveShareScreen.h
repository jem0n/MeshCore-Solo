#pragma once
// Live location sharing config tool. Tools › Live Share.
// Periodically broadcasts a [LOC] message to a chosen channel/contact while
// moving. Independent of (and able to run alongside) the 0-hop Auto-Advert.
// Included by UITask.cpp after AutoAdvertScreen.h.

#include "../GeoUtils.h"      // LOCATION_MSG_TAG
#include "../NodePrefs.h"

class LiveShareScreen : public UIScreen {
  UITask*    _task;
  NodePrefs* _prefs;
  bool       _dirty  = false;
  int        _sel    = 0;
  int        _scroll = 0;

  // Row model — headers are non-selectable section separators.
  enum Kind : uint8_t { K_HEADER, K_TRACK, K_AUTO, K_TARGET, K_MOVE, K_GAP, K_HB, K_SHARE_NOW };
  struct Row { Kind kind; const char* label; };
  static const int ROW_COUNT = 12;
  static Row rows(int i) {
    static const Row R[ROW_COUNT] = {
      { K_HEADER,     "Receive" },
      { K_TRACK,      "Track loc" },
      { K_HEADER,     "Send" },
      { K_AUTO,       "Auto share" },
      { K_HEADER,     "Target" },
      { K_TARGET,     "To" },
      { K_HEADER,     "Triggers" },
      { K_MOVE,       "Move" },
      { K_GAP,        "Min gap" },
      { K_HB,         "Heartbeat" },
      { K_HEADER,     "" },
      { K_SHARE_NOW,  "Share now" },
    };
    return R[i];
  }
  static bool selectable(Kind k) { return k != K_HEADER; }

public:
  LiveShareScreen(UITask* task, NodePrefs* prefs) : _task(task), _prefs(prefs) {}

  void enter() {
    _dirty = false;
    if (!selectable(rows(_sel).kind)) _sel = firstSelectable();
  }

  int firstSelectable() const {
    for (int i = 0; i < ROW_COUNT; i++) if (selectable(rows(i).kind)) return i;
    return 0;
  }

  // ── target helpers ─────────────────────────────────────────────────────────
  void currentTargetName(char* buf, int n) {
    if (!_prefs) { snprintf(buf, n, "none"); return; }
    if (_prefs->loc_share_target_type == 0) {
      ChannelDetails ch;
      if (the_mesh.getChannel(_prefs->loc_share_channel_idx, ch) && ch.name[0])
        snprintf(buf, n, "%s", ch.name);
      else
        snprintf(buf, n, "Ch %d", (int)_prefs->loc_share_channel_idx);
    } else {
      ContactInfo* c = the_mesh.lookupContactByPubKey(_prefs->loc_share_dm_prefix, NodePrefs::FAVOURITE_PREFIX_LEN);
      if (c) snprintf(buf, n, "%s", c->name);
      else   snprintf(buf, n, "(contact?)");
    }
  }

  // ── value label for a row ──────────────────────────────────────────────────
  void valueLabel(Kind k, char* buf, int n) {
    switch (k) {
      case K_TRACK:
        snprintf(buf, n, "%s", (_prefs && _prefs->track_shared_loc) ? "ON" : "OFF");
        break;
      case K_AUTO:
        snprintf(buf, n, "%s", (_prefs && _prefs->loc_share_enabled) ? "ON" : "OFF");
        break;
      case K_TARGET:
        currentTargetName(buf, n);
        break;
      case K_MOVE:
        snprintf(buf, n, "%um", (unsigned)NodePrefs::locShareMoveMeters(_prefs ? _prefs->loc_share_move_idx : 1));
        break;
      case K_GAP: {
        uint16_t s = NodePrefs::locShareIntervalSecs(_prefs ? _prefs->loc_share_interval_idx : 1);
        if (s < 60) snprintf(buf, n, "%us", (unsigned)s);
        else        snprintf(buf, n, "%um", (unsigned)(s / 60));
        break;
      }
      case K_HB: {
        uint16_t s = NodePrefs::locShareHeartbeatSecs(_prefs ? _prefs->loc_share_heartbeat_idx : 0);
        if (s == 0) snprintf(buf, n, "OFF");
        else        snprintf(buf, n, "%um", (unsigned)(s / 60));
        break;
      }
      default: buf[0] = '\0';
    }
  }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);
    display.drawCenteredHeader("LIVE SHARE");

    const int y0   = display.listStart();
    const int step = display.lineStep();
    const int valx = display.width() / 2 + 6;

    // Scroll window: keep the selected row visible.
    int vis = (display.height() - y0) / step;
    if (vis < 1) vis = 1;
    if (_sel < _scroll) _scroll = _sel;
    if (_sel >= _scroll + vis) _scroll = _sel - vis + 1;
    if (_scroll < 0) _scroll = 0;
    if (_scroll > ROW_COUNT - vis) _scroll = (ROW_COUNT > vis) ? ROW_COUNT - vis : 0;

    for (int i = _scroll; i < ROW_COUNT && i < _scroll + vis; i++) {
      int y = y0 + (i - _scroll) * step;
      Row r = rows(i);
      if (r.kind == K_HEADER) {
        if (r.label[0]) {
          display.setColor(DisplayDriver::LIGHT);
          display.setCursor(2, y);
          display.print(r.label);
        }
        continue;
      }
      bool sel = (i == _sel);
      display.drawSelectionRow(0, y - 1, display.width(), step - 1, sel);
      display.setCursor(4, y);
      display.print(r.label);
      char val[24];
      valueLabel(r.kind, val, sizeof(val));
      if (val[0]) {
        display.setCursor(valx, y);
        display.print(val);
      }
    }
    return 500;
  }

  void moveSel(int dir) {
    int i = _sel;
    for (int step = 0; step < ROW_COUNT; step++) {
      i = (i + dir + ROW_COUNT) % ROW_COUNT;
      if (selectable(rows(i).kind)) { _sel = i; return; }
    }
  }

  // Cycle/act on the selected row. `enter` distinguishes ENTER (which opens the
  // target picker / fires Share now) from LEFT/RIGHT (value cycling only).
  void activate(int dir, bool enter) {
    if (!_prefs) return;
    Kind k = rows(_sel).kind;
    switch (k) {
      case K_TRACK: _prefs->track_shared_loc ^= 1; _dirty = true; break;
      case K_AUTO: _prefs->loc_share_enabled ^= 1; _dirty = true; break;
      case K_TARGET:
        if (enter) { _task->pickLocShareTarget(); return; }  // full chooser
        cycleTarget(dir); _dirty = true;                     // L/R quick cycle
        break;
      case K_MOVE:
        _prefs->loc_share_move_idx = (uint8_t)((_prefs->loc_share_move_idx + (dir >= 0 ? 1 : NodePrefs::LOC_SHARE_MOVE_COUNT - 1)) % NodePrefs::LOC_SHARE_MOVE_COUNT);
        _dirty = true; break;
      case K_GAP:
        _prefs->loc_share_interval_idx = (uint8_t)((_prefs->loc_share_interval_idx + (dir >= 0 ? 1 : NodePrefs::LOC_SHARE_INTERVAL_COUNT - 1)) % NodePrefs::LOC_SHARE_INTERVAL_COUNT);
        _dirty = true; break;
      case K_HB:
        _prefs->loc_share_heartbeat_idx = (uint8_t)((_prefs->loc_share_heartbeat_idx + (dir >= 0 ? 1 : NodePrefs::LOC_SHARE_HEARTBEAT_COUNT - 1)) % NodePrefs::LOC_SHARE_HEARTBEAT_COUNT);
        _dirty = true; break;
      case K_SHARE_NOW: shareNow(); break;
      default: break;
    }
  }

  void cycleTarget(int dir) {
    struct T { uint8_t type, ch; uint8_t prefix[NodePrefs::FAVOURITE_PREFIX_LEN]; } list[24];
    int n = 0;
    ChannelDetails ch;
    for (int i = 0; i < 64 && n < 24; i++) {
      if (!the_mesh.getChannel(i, ch) || ch.name[0] == '\0') continue;
      list[n].type = 0; list[n].ch = (uint8_t)i; n++;
    }
    for (int i = 0; i < NodePrefs::FAVOURITES_COUNT && n < 24; i++) {
      const uint8_t* pre = _prefs->favourite_contacts[i];
      bool empty = true;
      for (int b = 0; b < NodePrefs::FAVOURITE_PREFIX_LEN; b++) if (pre[b]) { empty = false; break; }
      if (empty || !the_mesh.lookupContactByPubKey(pre, NodePrefs::FAVOURITE_PREFIX_LEN)) continue;
      list[n].type = 1; list[n].ch = 0;
      memcpy(list[n].prefix, pre, NodePrefs::FAVOURITE_PREFIX_LEN); n++;
    }
    if (n == 0) { _task->showAlert("No channels/favs", 1200); return; }
    int cur = -1;
    for (int i = 0; i < n; i++) {
      if (list[i].type != _prefs->loc_share_target_type) continue;
      if (list[i].type == 0) { if (list[i].ch == _prefs->loc_share_channel_idx) { cur = i; break; } }
      else if (memcmp(list[i].prefix, _prefs->loc_share_dm_prefix, NodePrefs::FAVOURITE_PREFIX_LEN) == 0) { cur = i; break; }
    }
    int nx = (cur < 0) ? 0 : ((cur + (dir >= 0 ? 1 : n - 1)) % n);
    _prefs->loc_share_target_type = list[nx].type;
    if (list[nx].type == 0) _prefs->loc_share_channel_idx = list[nx].ch;
    else memcpy(_prefs->loc_share_dm_prefix, list[nx].prefix, NodePrefs::FAVOURITE_PREFIX_LEN);
  }

  void shareNow() {
    int32_t lat, lon;
    if (!_task->currentLocation(lat, lon)) { _task->showAlert("No GPS fix", 1000); return; }
    char text[40];
    snprintf(text, sizeof(text), LOCATION_MSG_TAG "%.5f,%.5f", lat / 1e6, lon / 1e6);
    _task->shareToMessage(text);
  }

  bool handleInput(char c) override {
    if (c == KEY_CANCEL || c == KEY_CONTEXT_MENU) {
      if (_dirty) { the_mesh.savePrefs(); _dirty = false; }
      _task->gotoToolsScreen();
      return true;
    }
    if (c == KEY_UP)   { moveSel(-1); return true; }
    if (c == KEY_DOWN) { moveSel(+1); return true; }
    if (keyIsPrev(c))  { activate(-1, false); return true; }
    if (keyIsNext(c))  { activate(+1, false); return true; }
    if (c == KEY_ENTER) { activate(+1, true); return true; }
    return false;
  }
};
