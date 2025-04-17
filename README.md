# SheetMidi

A Pure Data external that converts a string (list in pd) of chord symbols into MIDI note numbers. Specify Chords, bars, chord-durations in beats and get midi note numbers for root, third, fifth, allnotes and randomnote from the current chord. advance time by sending tick as a message to the object. May the algorithmic composing begin!

## Features

- Define chord progressions with durations and bar markers
- Set time signature and/or use dot notation for setting individual chord durations
- Output specific chord tones (root, third, fifth), a random note or a list of all notes over eleven octaves (output notes can exceed the 127 midi range)
- Support for complex chord symbols (e.g., Cmaj7, Dm7b5, G6, C#m7#9)

## Outlets

1. First outlet (note_outlet): Outputs single MIDI note values
2. SEcond outlet (list_outlet): Outputs lists of MIDI notes (used for [all( command)
3. Third outlet (beat_outlet): Outputs current beat position
4. Fourth outlet (debug_outlet): Outputs chord symbols when debug is enabled

## Input Commands

- `[bang(`: Output the current note value
- `[note(`: Output the current note value
- `[root(`: Output the root note of the current chord
- `[third(`: Output the third note of the current chord
- `[fifth(`: Output the fifth note of the current chord
- `[all(`: Output a list of all possible MIDI notes (0-127) that are part of the current chord through the list outlet
- `[tick(`: Advances the beat counter (typically connected to a metro)
- `[beat n(`: Resets the beat counter to position n and outputs the new position

#### Right Inlet

- **Symbol**: Send a single chord symbol (e.g., `[C(`, `[Dm7b5(`, `[G#7(`, `[Bb6(`)
- **List**: Send a sequence of chords with durations
  - Format: `chord [. . .] | [chord [. . .]] | ...`
  - Example:
    ```
    [Ebmaj7 Eb7 Ab6 . | Ebmaj7 Eb7 Ab6 . | Bbm7 Eb13 | Bbm7 E9#11 Eb9 A7b5 | Abmaj7 | Db9#11 | Gm7 C7b9 | Fm11 Bb7 | Eb13 Ab9 | Eb13 | Ab11(
    ```
  - Bar markers (|) separate measures
  - **Timing behavior**:
    - **Without dot notation**: When no dots are used in a bar, the beats are distributed evenly among the chords in that bar. For example, in 4/4 time, if a bar contains two chords, each chord gets 2 beats.
    - **With dot notation**: As soon as dot notation is present in a bar behavior switches to this: Each chord starts with a duration of 1 beat, and each dot (.) after a chord extends its duration by 1 beat. This allows for precise control over chord durations within a bar.
- **time [value]**: Set the time signature (e.g., `[time 4(` for 4/4)
- **beat [value]**: Reset the beat counter to a specific position (e.g., `[beat 0(` to start from beginning, `[beat 13(` to jump to beat 13). The value wraps around automatically based on the total sequence duration.

#### Supported Chord Symbols

The following chord symbol formats are supported:
- **Root Notes**: `C`, `D`, `E`, `F`, `G`, `A`, `B` (can be modified with `b` or `#` for flats/sharps, e.g. `C#`, `Bb`)
- **Minor Chords**: Add `m`, `mi`, `min`, or `MI` (e.g. `Cm`, `Ami`, `Bbmin`)
- **Diminished Chords**: Add `dim` (e.g. `Cdim`, `F#dim`)
- **Major Seventh**: Can use `maj7`, `MAJ7`, `Maj7`, `MA7` (e.g. `Cmaj7`, `FMAJ7`)
- **Extensions**: Add numbers for intervals (e.g. `C7`, `G6`, `Dm9`, `Fmaj79`, `F#13`)
- **Modified Extensions**: Use `b` or `#` before the interval number (e.g. `C7b5`, `Dm7b9`, `G#7#11`)

Examples of valid chord symbols:
- `C` (C major triad)
- `Dm7` (D minor seventh)
- `Ebmaj7` (E-flat major seventh)
- `F#m7b5` (F-sharp minor seventh flat five)
- `G13` (G thirteenth)
- `Abm9` (A-flat minor ninth)
- `Bb7#11` (B-flat seventh sharp eleven)
- `C#dim` (C-sharp diminished)

## Installation

### Pre-built Binaries

1. Download the appropriate binary for your platform from the releases page
2. See below for how to make external available in pd

### Building from Source

#### Prerequisites

- Pure Data development headers (`m_pd.h`)
- C compiler (gcc, clang, or MSVC)
- Make

#### Build Instructions

1. Clone the repository:
   ```