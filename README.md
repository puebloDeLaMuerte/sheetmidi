# SheetMidi

A Pure Data external that converts a string (list in pd) of chord symbols into MIDI note numbers. SheetMidi allows you to create musical sequences by specifying chords and their durations, then play back specific chord tones or random notes from the current chord.

## Features

- Define chord progressions with durations and bar markers
- Set time signature
- Output specific chord tones (root, third, fifth) or random notes
- Debug output for monitoring current chord
- Support for complex chord symbols (e.g., Cmaj7, Dm7b5, G6, C#m7#9)

## Usage in Pure Data

### Input Commands

#### Main Inlet (Left)

- **bang**: Outputs information about the current chord sequence
- **note**: Outputs a random note from the current chord
- **root**: Outputs the root note of the current chord
- **third**: Outputs the third note of the current chord
- **fifth**: Outputs the fifth note of the current chord
- **tick**: Advances the beat counter (typically connected to a metro)

#### Right Inlet

- **Symbol**: Send a single chord symbol (e.g., "C", "Dm7", "G7")
- **List**: Send a sequence of chords with durations (e.g., "C . . | Dm7 . | G7 . . . ")
  - Format: `chord [. . .] | [chord [. . .]] | ...`
  - Bar markers (|) separate measures
  - **Timing behavior**:
    - **Without dot notation**: When no dots are used in a bar, the beats are distributed evenly among the chords in that bar. For example, in 4/4 time, if a bar contains two chords, each chord gets 2 beats.
    - **With dot notation**: As soon as dot notation is present in a bar behavior switches to this: Each chord starts with a duration of 1 beat, and each dot (.) after a chord extends its duration by 1 beat. This allows for precise control over chord durations within a bar.
- **time [value]**: Set the time signature (e.g., "time 4" for 4/4) 

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

- Pure Data development headers
- C compiler (gcc, clang, or MSVC)
- Make

#### Build Instructions

1. Clone the repository:
   ```
   git clone https://github.com/puebloDeLaMuerte/sheetmidi.git
   cd sheetmidi
   ```

2. Build the external:
   ```
   make
   ```

3. Install the external:
   ```
   make install
   ```

## License

[Add license information here]

## Author

[Add author information here]

