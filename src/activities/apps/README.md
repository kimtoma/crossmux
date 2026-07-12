# Apps

The `apps/` directory holds non-reader sub-applications. They share the home-screen "Apps" tile, `AppsMenuActivity`, and the conventions below.

Shipped apps:

- `reading-stats/` — reading statistics
- `weread/` — WeRead (Chinese builds only, `ENABLE_CHINESE_VERSION`)
- `standby/` — standby / clock screen

Games and toys (sudoku, gomoku, minesweeper, 2048, cellular, avatar, chinese-chess) are **not** included in firmware.

Reader, file browser, settings, OPDS, etc. are core features under `activities/<feature>/`, not under `apps/`.

---

## Directory layout

```
apps/
├── AppsMenuActivity.{h,cpp}   # dispatcher — see "Adding a new app"
├── reading-stats/
├── standby/
└── weread/                    # gated by ENABLE_CHINESE_VERSION
```

**File naming** — keep the app-name prefix (`ReadingStatsMenuActivity.cpp`). Class names stay globally unique without namespaces.

---

## Adding a new app

### 1. Create the app

```
apps/<myapp>/
  MyAppActivity.{h,cpp}   # extends Activity
```

Back must return to the Apps menu:

```cpp
if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
  activityManager.goToApps();
}
```

### 2. Add `goToMyApp()` in `ActivityManager.{h,cpp}`

### 3. Append one row to `kAppEntries` in `AppsMenuActivity.cpp`

### 4. Add i18n key + `UIIcon` + Lyra `iconForName` case

### 5. Conditional apps (e.g. WeRead)

Use `#ifdef ENABLE_CHINESE_VERSION` at every reference site, plus `build_src_filter` in `platformio.ini`:

- base: `-<activities/apps/<app>/>`
- gated env: `+<activities/apps/<app>/>`

Do not add inner ifdefs inside the app's own sources.

---

## Navigation

```
Home  ──Confirm "Apps"──▶  AppsMenu  ──Confirm row──▶  <App>
  ▲                            │
  └──────Back──────────────────┘    ◀──Back──  <App>
```

## Resource budget

Same 380KB RAM ceiling as the reader: alloc in `onEnter()`, free in `onExit()`; no large stack buffers; debounce SPIFFS writes.
