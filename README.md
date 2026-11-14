# Fuga

A basic DAW for composing fugues. Native macOS app, C++20 and JUCE 8. Licensed under GPLv3.

## Build and run

```bash
git submodule update --init
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --target Fuga fuga_core_tests -j10
./build/fuga_core_tests
open build/Fuga_artefacts/Release/Fuga.app
```

Only the Xcode Command Line Tools and CMake are needed.

## Layout

- `core/` Score model, fugue rules and generation. Plain C++20 with no JUCE, covered by `tests/CoreTests.cpp`.
  - `Score.h` voices (layers), clips typed as subject, answer, countersubject or free, notes, sections.
  - `Theory.*` keys, intervals, real and tonal answers.
  - `Rules.*` counterpoint checks (parallels, crossing, range, accented dissonance) and per-section rules.
  - `Generate.*` dynamic-programming line generator and the exposition builder.
- `app/` JUCE application.
  - `Document.*` open project, undo, selection, JSON `.fuga` files.
  - `AudioEngine.*` playback, built-in organ, VST3/AU hosting per voice, MIDI input and recording.
  - `TimelineView.*` voice lanes, clips, section row. `PianoRollView.*` note editor.
  - `MainComponent.*` toolbar, menus, fugue check list.

`Fuga --snapshot out.png --demo` renders the window to a PNG and quits, which is handy for checking UI changes.

## Using it

- Double-click a voice lane to create a clip, then click in the piano roll to write notes. Drag to move, drag the right edge to resize, double-click or Delete to remove. Shift or Cmd drag selects several.
- Right-click a clip to set its role or to generate an answer, a countersubject, or a whole exposition from it.
- Right-click a layer's name and choose "Lock layer to role" to make it a subject, answer or countersubject layer. A locked layer gives every clip that role, fills new clips with the matching material, and reports any clip that breaks the role in the Fugue check list.
- Drag along the bar ruler to set a loop region (hold Option for finer steps), and press L or the Loop button to turn looping on and off. Right-click the ruler to loop the selected clip or section.
- Pick the tonic and scale (major, the three minors, and the modes) next to Key. "Scale lock" disables piano roll rows outside the scale; "MIDI filter" ignores out-of-scale notes from a MIDI keyboard.
- Cmd+D duplicates the selected clip directly after itself.
- Double-click the Sections row to add a section, right-click to set its type and key. Each type has rules, shown in the Fugue check list.
- Click a voice's instrument button to pick the built-in organ or a scanned VST3/AU instrument.
- Record (R) captures a MIDI keyboard into the selected voice. Space plays and stops.
