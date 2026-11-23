# NODAL — a performance, resynthesised through a wavetable

A VST3 effect that turns a played or sung line (voice, flute, violin, guitar, sax…) into a
synthesiser that follows it: the same notes, the same dynamics, the same vowels — played by
a wavetable you choose. A MIX control sets how much of the transformation you hear.

It is an effect, not an instrument: no MIDI, the performance is the controller.

## How it works

Every 5.3 ms (one *hop*) the input is analysed and drives up to eight wavetable voices.

| Stage | What it does | Measured |
|---|---|---|
| **Pitch** | YIN, with its difference function computed by FFT (O(N log N)); a median of three frames removes octave slips; above 1.5 kHz the pitch is refined on the fundamental's spectral peak | G1..G7: mean error ≤ 2 cents at 44.1 / 48 / 96 kHz, no octave error |
| **Envelope** | 128-point spectral envelope (20 Hz – 20 kHz) following the harmonic peaks; its broad tilt is removed, so formants can be imprinted without darkening the table | |
| **Level** | Sample-by-sample envelope follower (1 ms attack, RELEASE), turned into an RMS by the input's own crest factor | wet within 0.9 dB of the input, -30 to -6 dBFS, 1 to 8 voices |
| **Voices** | Each voice rebuilds its single cycle once per hop from the table's harmonic spectra: only harmonics under 0.45 × the sample rate (band-limited by construction), shaped by the input's formants (FORMANT), normalised to RMS 1, crossfaded over the hop | off-harmonic energy ≤ -49 dB from C4 to G7 (saw, sync ×8, 3-step digital) |
| **Spray** | Each voice reads its own moment of the last 2 s, grain by grain (Tukey window, staggered starts): a phrase becomes a cloud of its own notes. FREEZE holds one moment | |
| **Breath** | What is not periodic (breath, bow, consonants) is resynthesised as noise, louder when the input is less periodic. Four colours: FLUTE (jet noise in narrow bands on the synth's own harmonics, over a soft hiss), AIR (bright hiss), WHISPER (the input's vowels), INPUT (the input's own spectrum, the darkest) | A touch, not a layer: squared curve, at most 10 dB under the synth (FLUTE), -30 dB around 30 %; off by default, in 14 presets at -32 to -17 dB. Spectral centroid on an A4 voice: FLUTE 1.9 kHz (36 % on the harmonics, 8 % for plain noise), AIR 3.7 kHz, WHISPER 1.3 kHz, INPUT 0.7 kHz |

**Latency.** ALIGNED (default) delays the dry and the envelope by half an analysis window
(1024 samples at 48 kHz, 21 ms), reported to the host: the synth lands on the notes and the
DAW compensates. LIVE adds nothing, for playing through it (the synth then lags ~25 ms).
The switch is not automatable: every latency change restarts the plugin in the host
(automated, it stalled the audio for milliseconds in the host test).

**CPU** (host test, one core of an i7-12700KF): 1.5 – 3 % on average at 44.1 / 48 kHz,
2.5 – 4 % at 96 kHz. Worst single block: 16 % of its budget at 48 kHz / 64 samples, 48 % at
96 kHz / 64 samples (the block where a hop's analysis lands).

**Steady.** A quick round trip of the pitch (an octave or a fifth for a few tens of ms, at a
note's start or in its middle) is not followed: the synth stays on the median of the pitches
over the last 2 × STEADY; a pitch within 2.5 semitones of it (vibrato, a step, a third) passes
at once. A real leap becomes the median after STEADY and is followed from then on. Measured
(`--analyse`, five round trips of 10 – 45 ms): 3 reach the synth with STEADY off, 1 at 40 ms
(the default; the 45 ms one), none at 80 ms; real leaps arrive 37 ms (40 ms) or 79 ms
(80 ms) later. The SPRAY grains read the same steadied line.

**Range.** RANGE keeps the synth between two notes (at least an octave apart) by moving
each note by whole octaves. The octave holds while the note stays within a semitone of the
range, so a note on an edge does not flip back and forth; when it must change, the octave
nearest the last note wins. With one octave, an octave slip of the tracker never reaches the
synth. 1 / 2 / 3 OCT place the range on the KEY note, around the median of the notes played
over the last 4 s (one wrong octave does not move it). On a line over four octaves with
leaps (`--analyse`): 0 notes outside the range, 17 leaps of 9+ semitones with the range off,
5 with one octave.

**Monophonic.** The tracker follows one note at a time: chords confuse it (it finds a
common root or jumps). For a chordal guitar, FIXED pitch mode keeps a drone that follows the
playing's dynamics and brightness.

## Controls

| Section | Controls |
|---|---|
| **PITCH** | TRACK / FIXED, TRANSPOSE (±24 st), FINE, GLIDE, QUANTIZE (Off, Chromatic and the 35 scales of Ableton Live's list, from Major to Messiaen 7) and KEY, FIXED NOTE, RANGE |
| **TABLE** | 12 factory tables or your own, POSITION, TRACK (the input's brightness moves the position), FORMANT (imprint of the input's formants), FORMANT SHIFT |
| **UNISON** | VOICES 1 – 8, DETUNE, WT SPREAD (each voice at another position), WIDTH |
| **SPRAY** | SPRAY (how far back, up to 2 s), GRAIN (20 – 500 ms), FREEZE |
| **DYNAMICS** | RELEASE (time to fall 30 dB, 50 ms – 4 s), GATE, VOICED ONLY (mutes the synth on unpitched sounds), SENSITIVITY (pitch detection threshold), STEADY (0 – 120 ms: round trips shorter than this are ignored) |
| **COLOR** | BREATH and its mode (FLUTE / AIR / WHISPER / INPUT), SUB (sine an octave down), TONE (low-pass), DRIVE |
| **OUTPUT** | ALIGNED / LIVE, OUTPUT, MIX |

60 presets: BASE, VOIX, SOUFFLE, CORDES, GUITARE, LEAD, PAD, TEXTURE, FX.

Gestures (those of STASIS, and more):

| Gesture | Effect |
|---|---|
| Drag (left / right or up / down) | moves the control; a click on a bar jumps there |
| Shift or Ctrl while dragging, scrolling or stepping | 10× finer, can be pressed mid-drag without a jump |
| Mouse wheel | 1 % a notch; on a menu or a switch, the next choice |
| ↑ ↓ over a control (or focused) | 1 % a step, 0.1 % with Shift / Ctrl; Home / End: min / max; Delete: default |
| Double-click | back to the default |
| Double-click the value (or Enter) | type it: "70", "-12", "2 s", "350 ms", "4.5k", "C3", "off" |
| Right-click | default, type a value, minimum, maximum; the choices of a menu |
| Open menu | ↑ ↓ Enter Escape, and letters to jump ("hi" → Hirajoshi) |
| ← → | previous / next wavetable |
| RANGE | drag a handle, drag the band to move it, drag on the keys (when FULL) to draw a range, click outside to bring the nearest handle; wheel / ↑ ↓: the whole range by a semitone; type "C2 C5", "2 oct", "full"; double-click: FULL |
| Plate | double-click: whole monitor (Escape leaves); mode: ◀ ▶ at the top, the wheel, 1 – 9 and 0, ← → in full screen; colour: ◀ ▶ at the bottom, Shift + wheel, ↑ ↓ |

The line at the bottom of the window says what the hovered control does.

### Your wavetables

**FILE** loads one file; **FOLDER** picks a folder. Every `.wav`, `.aif` and `.flac` in it
**and its subfolders** is then one arrow away: ◀ ▶ (or ← → on the keyboard) step through them
in order, the name opens the list grouped by subfolder. The folder is remembered.

Serum-style tables (2048-sample frames, `clm` chunk) are read as such; a file whose length is
a multiple of 2048 is cut in 2048-sample frames; a short file is one single cycle. Up to 256
frames. The table is saved inside the session (16-bit), so a project opens anywhere even
without the file.

## The interface

Black with film grain (like STASIS), pastel section blocks. In the centre, in black and white,
a Chladni plate: 65 536 grains of sand on a square plate, kicked where the plate moves and
sliding towards where it does not, gathering on the nodal lines. The synth's pitch chooses the
modes (higher notes, denser figures), its first harmonics weigh them, the table position flips
the figure's symmetry, the output level shakes the plate. WebGL2, simulated on the GPU.

Ten visual modes, all drawn from the same vibrating field: SAND (the square plate), DISC (a
round plate: Bessel modes, circles and spokes), SILK (the grains flow along the level lines
and leave trails), CONTOUR (the field as level lines), MOIRE (two ring gratings bent by the
field in opposite directions), ECHO (the nodal lines fed back into themselves, zooming out),
MEMBRANE (the field as a lit surface swinging up and down), RIDGES (54 stacked lines lifted
by the field, the nearer hiding the farther), MESH (a grid pushed along the field's slope),
HALFTONE (a print screen whose dots grow where the plate moves).

Twenty colour skins, chosen apart from the mode: every mode draws in grey, a last pass maps
the grey through the skin. MONO (black and white, the default), eleven other duotones (BONE, MINT,
LILAC, PEACH, BUTTER, SKY, ROSE, BLUEPRINT, PAPER, PHOSPHOR, AMBER) and eight four-colour
gradients (AURORA, SUNSET, PASTEL, EMBER, GLACIER, ORCHID, THERMAL, OPAL). Mode and skin are
remembered.

Double-click the plate: in the plugin it opens alone in a borderless window over the whole
monitor the plugin is on (its own web view, fed the same live state; the plate in the
plugin window rests meanwhile); Escape or a double-click closes it. In a browser, the page
goes full screen.

`design/ui.html` also opens in a browser (`node dev/serve.js`, then http://localhost:8771/),
with a simulated sung phrase.

## Build

Windows: Visual Studio 2022 (C++ desktop workload), CMake ≥ 3.22, the WebView2 SDK:

```
nuget install Microsoft.Web.WebView2 -OutputDirectory C:\webview2
cmake -S . -B build -A x64 -DNODAL_BUILD_RENDER=OFF -DJUCE_WEBVIEW2_PACKAGE_LOCATION=C:\webview2
cmake --build build --config Release --target Nodal_VST3 nodal_hosttest
build\plugin\nodal_hosttest_artefacts\Release\nodal_hosttest.exe build\plugin\Nodal_artefacts\Release\VST3\NODAL.vst3
```

Install: copy `NODAL.vst3` into `C:\Program Files\Common Files\VST3\`.

Measurements and offline renders (no JUCE needed):

```
cmake -S . -B build-render -A x64 -DNODAL_BUILD_PLUGIN=OFF
cmake --build build-render --config Release
build-render\Release\nodal_render.exe --analyse
build-render\Release\nodal_render.exe --all input.wav renders\
```

## Layout

| Path | |
|---|---|
| `dsp/include/nodal/` | The DSP, C++20, header-only, no dependency: `fft` (radix-2 real FFT), `analyzer` (YIN, envelope, history), `wavetable` (spectra, factory tables, file loading), `engine` |
| `plugin/Source/Params.h` | Every parameter, its unit and label (copied in `design/ui.html`) |
| `plugin/Source/Presets.h` | 60 factory presets |
| `plugin/Source/Plugin*.cpp` | JUCE 8 processor (folder browsing, state) and WebView editor |
| `plugin/HostTest/` | `nodal_hosttest`: loads the VST3 like a DAW and stresses it |
| `tools/render/` | `nodal_render`: measurements and renders |

## License

GPLv3 (JUCE is used under the AGPLv3 / GPLv3 terms).
