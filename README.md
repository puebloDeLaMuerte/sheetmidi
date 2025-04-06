# SheetMidi

A Pure Data external that converts a string (list in pd) of chord symbols into MIDI note numbers. Specify Chords, bars, chord-durations in beats and get midi note numbers for root, third, fifth, allnotes and randomnote from the current chord. advance time by sending tick as a message to the object. May the algorithmic composing begin!

## Features

- Define chord progressions with durations and bar markers
- Set time signature
- Output specific chord tones (root, third, fifth) or random notes
- Debug output for monitoring current chord
- Support for complex chord symbols (e.g., Cmaj7, Dm7b5, G6, C#m7#9)

## Usage in Pure Data

### Input Commands

#### Main Inlet (Left)

- `[bang(`: Outputs information about the current chord sequence
- `[note(`: Outputs a random note from the current chord
- `[root(`: Outputs the root note of the current chord
- `[third(`: Outputs the third note of the current chord
- `[fifth(`: Outputs the fifth note of the current chord
- `[tick(`: Advances the beat counter (typically connected to a metro)

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
- **Extensions**: Add numbers for intervals (e.g. `C7`, `Dm9`, `Fmaj7`, `G13`)
- **Modified Extensions**: Use `b` or `#` before the interval number (e.g. `C7b5`, `Dm7b9`, `G#7#11`)
- **Major Seventh**: Can use `maj7`, `MAJ7`, `Maj7`, `MA7` (e.g. `Cmaj7`, `FMAJ7`)

Examples of valid chord symbols:
- `C` (C major triad)
- `Dm7` (D minor seventh)
- `Ebmaj7` (E-flat major seventh)
- `F#m7b5` (F-sharp minor seventh flat five)
- `G13` (G thirteenth)
- `Abm9` (A-flat minor ninth)
- `Bb7#11` (B-flat seventh sharp eleven)
- `C#dim` (C-sharp diminished)

### Outputs

- **Left outlet**: MIDI note values (0-127)
- **Right outlet**: Chord symbols for debugging

## Installation

### Pre-built Binaries

1. Download the appropriate binary for your platform from the releases page
2. Place the binary in your Pure Data externals folder:
   - macOS: `~/Library/Pd/externals/`
   - Linux: `~/.pd-externals/`
   - Windows: `C:\Program Files\Pd\extra\`

### Building from Source

#### Prerequisites

- Pure Data development headers (`m_pd.h`)
- C compiler (gcc, clang, or MSVC)
- Make

#### Build Instructions

1. Clone the repository:
   ```
   git clone https://github.com/puebloDeLaMuerte/sheetmidi.git
   cd sheetmidi
   ```

2. Get Pure Data header:
   - Download `m_pd.h` from [Pure Data's repository](https://github.com/pure-data/pure-data/blob/master/src/m_pd.h)
   - Place it in the `src/include` directory (the directory will be present after cloning)

3. Build the external:
   ```
   make
   ```

The compiled external will be in the `lib` directory. You have two options to make it available to Pure Data:

1. Copy it to your Pure Data externals folder:
   - macOS: `~/Library/Pd/externals/`
   - Linux: `~/.pd-externals/`
   - Windows: `C:\Program Files\Pd\extra\`

2. Add the path to Pure Data's startup flags:
   ```
   -path /Path/To/sheetmidi/lib
   ```
   (This is what worked for me, using )Pd-L2Ork)

## License

GNU Lesser General Public License (LGPL) v3

Copyright (c) 2024 Philipp Tögel

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU Lesser General Public License for more details.

This means you can:
- Use SheetMidi in any project (including commercial projects)
- Modify the code
- Distribute the code
- Link to SheetMidi from other software (including proprietary software)

But you must:
- Share any modifications to SheetMidi itself under LGPL
- Not sell SheetMidi as a standalone proprietary product
- Include the LGPL license and copyright notice with any distribution

For the full license text, see <https://www.gnu.org/licenses/lgpl-3.0.en.html>

## Author

Philipp Tögel, Berlin

