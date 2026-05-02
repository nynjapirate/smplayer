# SMPlayer (gbit00 fork)

This is a personal fork of [SMPlayer 23.12.0](https://github.com/smplayer-dev/smplayer) with a set of feature patches that target large-folder use, VLC-style playlist UX, hover-thumbnails over the seekbar, keyboard-driven file management, and a more configurable toolbar editor.

Upstream's homepage and documentation: [smplayer.info](https://smplayer.info).

## What's different from upstream

| Phase | Change |
|---|---|
| **B** | Folder-load freeze fix. `Playlist::addDirectory` now runs an iterative BFS on a `QtConcurrent::run` worker thread with a cancellable progress dialog. Per-file metadata (duration etc.) is populated asynchronously via `ffprobe` — never the synchronous `InfoProvider::getInfo` mpv-spawn-per-file that froze the GUI on 50+ files. |
| **C** | Playlist UX overhaul. Forced top-level `Qt::Window` mode (full edge resize + own taskbar entry); manual `takeRow`/`insertRow` drag-drop reorder that preserves `PLItem` identity; auto-renumber position column; thumbnail-row view mode (right-click → "Show thumbnails (row preview)" + size submenu); per-state row colours (playing / selected / playing+selected) under `[playlist_colors]`. |
| **D** | Haruna-style hover-thumbnail above the seekbar. Persistent headless `mpv --idle=yes --vo=null --input-ipc-server=…` per current file, JSON-RPC over `QLocalSocket`. The `TimeSlider` widget itself is 2× tall while the groove and handle paint at natural size centred. Right-click on seekbar → enable / size / quality submenu. |
| **E** | Custom move locations. 10 `move_to_folder_N` actions plus `delete_current_file`, configured via a new Preferences page. Triggered move uses `QFile::rename` (cross-FS fallback to copy+remove); the playlist row vanishes but mpv keeps playing the in-flight file. Toggle `Create shortcuts (symbolic links) instead of moving files` swaps `rename` for `QFile::link`. |
| **F** | Numpad / main-row keyboard separation. `shortcutgetter.cpp` was explicitly swallowing `Qt::KeypadModifier`; now records `"Num+"`. `BaseGui::keyPressEvent`'s `NUMPAD_WORKAROUND` lookup includes the full modifier set. |
| **G** | Toolbar editor enhancements. Per-action icon/text overrides under `[toolbar_action_overrides]/<name>/{icon,text}`. Direct **Change Icon…** / **Change Text…** / **Reset Button** buttons in the toolbar editor (no in-between dialog). Graphical icon picker with hint-based ordering, search, category filter, and file-browse fallback — loads icons by direct filesystem path so theme-registration gaps don't hide entries. Per-toolbar **Icon Only (fallback to text)** mode persisted under `[toolbar_settings]/<toolbar>/icon_only_mode`. |
| **H** | Fork-distinct application icon. `Images::icon("logo")` prefers `smplayer.png` next to the executable. |

Other isolated patches: switched `MPVProcess::setAspect` to `set video-aspect-override` (mpv 0.37 [#893](https://github.com/smplayer-dev/smplayer/issues/893)); dropped `qApp->processEvents()` from `Playlist::filterEditChanged`; force-disabled `dockable_playlist` so the playlist is always a true Qt::Window in this fork.

## New settings keys (all in `smplayer.ini`, alongside upstream's)

```ini
[hover_thumbnails]
enabled=true
width_px=240
quality=85

[playlist_thumbnails]
enabled=false
row_height=96

[playlist_colors]
playing=#ffd54f
selected=#4fc3f7
playing_selected=#ff7043

[custom_move]
slots\1\path=/home/you/5StarVideos
slots\1\label=5 stars
create_shortcuts=false

[toolbar_action_overrides]
show_file_properties\icon=media-tape
show_file_properties\text=Tape

[toolbar_settings]
toolbar1\icon_only_mode=true
```

## Build

```sh
cd src
qmake -qt=5 PREFIX=/usr
make -j$(nproc)
# do NOT make install — run from src/smplayer side-by-side with /usr/bin/smplayer
```

`FIND_SUBTITLES` and `GLOBALSHORTCUTS` are commented out in `src/smplayer.pro` because they need `qtdeclarative5-dev` and `qtbase5-private-dev` respectively. Re-enable them and `apt install` those packages if you want OpenSubtitles search or global hotkeys.

## License

GPLv2-or-later, same as upstream SMPlayer. See `Copying.txt`.
