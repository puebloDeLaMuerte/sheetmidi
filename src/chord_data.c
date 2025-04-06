#include "m_pd.h"
#include "chord_data.h"
#include <string.h>
#include <ctype.h>

void debug_print_chord(const char* prefix, const t_chord_data* chord) {
    post("%s: Root: %d, Intervals:", prefix, chord->root_offset);
    for (int i = 0; i < chord->num_intervals; i++) {
        post("  %d", chord->intervals[i]);
    }
}

t_chord_data parse_chord_symbol(t_symbol *sym) {
    t_chord_data chord = {
        .original = sym,
        .root_offset = 0,
        .num_intervals = 0
    };
    memset(chord.intervals, -1, sizeof(chord.intervals));
    
    const char *str = sym->s_name;
    
    post("SheetMidi DEBUG: Starting to parse chord symbol: '%s'", str);
    
    // Skip leading whitespace and non-printable characters
    while (*str && (!isprint((unsigned char)*str) || isspace((unsigned char)*str))) {
        post("SheetMidi DEBUG: Skipping non-printable/whitespace char: ASCII %d", (int)(unsigned char)*str);
        str++;
    }
    
    // If string is empty after trimming
    if (!*str) {
        post("SheetMidi DEBUG: Empty chord symbol after trimming whitespace");
        return chord;
    }
    
    int pos = 0;
    post("SheetMidi DEBUG: Starting root note parsing at position %d, char '%c'", pos, str[pos]);
    
    // 1. Parse root note
    if (!isprint((unsigned char)str[pos])) {
        post("SheetMidi DEBUG: Invalid root note (non-printable character, ASCII %d)", (int)(unsigned char)str[pos]);
        return chord;
    }
    
    switch(str[pos]) {
        case 'C': chord.root_offset = 0; break;
        case 'D': chord.root_offset = 2; break;
        case 'E': chord.root_offset = 4; break;
        case 'F': chord.root_offset = 5; break;
        case 'G': chord.root_offset = 7; break;
        case 'A': chord.root_offset = 9; break;
        case 'B': chord.root_offset = 11; break;
        default: 
            post("SheetMidi DEBUG: Invalid root note '%c' at position %d", str[pos], pos);
            return chord;
    }
    post("SheetMidi DEBUG: Root note parsed, offset = %d", chord.root_offset);
    pos++;
    
    // Root modifier
    if (str[pos]) {
        post("SheetMidi DEBUG: Checking root modifier at position %d, char '%c'", pos, str[pos]);
    }
    if (str[pos] == 'b') {
        chord.root_offset = (chord.root_offset - 1 + 12) % 12;
        post("SheetMidi DEBUG: Applied flat modifier to root, new offset = %d", chord.root_offset);
        pos++;
    } else if (str[pos] == '#') {
        chord.root_offset = (chord.root_offset + 1) % 12;
        post("SheetMidi DEBUG: Applied sharp modifier to root, new offset = %d", chord.root_offset);
        pos++;
    }
    
    // Always add root note
    chord.intervals[chord.num_intervals++] = 0;
    post("SheetMidi DEBUG: Added root interval (0), num_intervals = %d", chord.num_intervals);
    
    // 2. Check for minor indicator
    if (str[pos]) {
        post("SheetMidi DEBUG: Checking for minor indicator at position %d, char '%c'", pos, str[pos]);
    }
    int third = 4;  // Default to major third
    int fifth = 7;  // Default to perfect fifth
    if (str[pos] == 'm') {
        third = 3;  // Minor third
        pos++;
        post("SheetMidi DEBUG: Found minor indicator 'm', setting third = 3");
        // Check for 'min' or 'mi'
        if (strncmp(&str[pos], "in", 2) == 0) {
            post("SheetMidi DEBUG: Found extended minor indicator 'min'");
            pos += 2;
        } else if (str[pos] == 'i') {
            post("SheetMidi DEBUG: Found extended minor indicator 'mi'");
            pos++;
        } else if (str[pos] == 'a' && str[pos + 1] == 'j') {
            // If we see 'maj', this is actually not a minor chord
            third = 4;  // Revert to major third
            pos--;  // Go back to 'm' to let the maj parsing handle it
            post("SheetMidi DEBUG: Found 'maj', reverting to major third");
        }
    } else if (str[pos] == 'M' && str[pos + 1] == 'I') {
        third = 3;  // Minor third
        pos += 2;
        post("SheetMidi DEBUG: Found minor indicator 'MI', setting third = 3");
    } else if (strncmp(&str[pos], "dim", 3) == 0) {
        third = 3;  // Minor third
        fifth = 6;  // Diminished fifth
        pos += 3;
        post("SheetMidi DEBUG: Found diminished indicator 'dim', setting third = 3, fifth = 6");
    }
    chord.intervals[chord.num_intervals++] = third;
    chord.intervals[chord.num_intervals++] = fifth;
    post("SheetMidi DEBUG: Added third (%d) and fifth (%d), num_intervals = %d", third, fifth, chord.num_intervals);
    
    // 3. Parse remaining intervals and modifications
    post("SheetMidi DEBUG: Starting to parse remaining intervals at position %d", pos);
    int loop_guard = 0;  // Safety counter to prevent infinite loops
    while (str[pos] != '\0' && loop_guard < 100) {  // Add reasonable maximum iterations
        loop_guard++;
        post("SheetMidi DEBUG: Loop iteration %d at position %d, char '%c'", loop_guard, pos, str[pos]);
        
        // Skip any whitespace
        while (str[pos] && isspace((unsigned char)str[pos])) {
            post("SheetMidi DEBUG: Skipping whitespace at position %d", pos);
            pos++;
        }
        if (!str[pos]) break;
        
        int modifier = 0;
        
        if (str[pos] == 'b') {
            modifier = -1;
            pos++;
            post("SheetMidi DEBUG: Found flat modifier, setting modifier = -1");
        } else if (str[pos] == '#') {
            modifier = 1;
            pos++;
            post("SheetMidi DEBUG: Found sharp modifier, setting modifier = 1");
        } else if (strncmp(&str[pos], "maj", 3) == 0 || strncmp(&str[pos], "MAJ", 3) == 0 || 
                   strncmp(&str[pos], "Maj", 3) == 0) {
            modifier = 1;
            pos += 3;
            post("SheetMidi DEBUG: Found major modifier (maj/MAJ/Maj), setting modifier = 1");
        } else if (strncmp(&str[pos], "MA", 2) == 0) {
            modifier = 1;
            pos += 2;
            post("SheetMidi DEBUG: Found major modifier (MA), setting modifier = 1");
        }
        
        if (isdigit(str[pos])) {
            post("SheetMidi DEBUG: Processing numeric interval at position %d", pos);
        }
        
        while (isdigit(str[pos])) {
            int interval = str[pos] - '0';
            pos++;
            post("SheetMidi DEBUG: Found digit %d", interval);
            
            if (isdigit(str[pos]) && !isdigit(str[pos + 1])) {
                interval = interval * 10 + (str[pos] - '0');
                pos++;
                post("SheetMidi DEBUG: Found two-digit interval %d", interval);
            }
            
            int semitones;
            switch(interval) {
                case 5:
                    post("SheetMidi DEBUG: Processing fifth interval");
                    chord.intervals[2] = 7 + modifier;
                    post("SheetMidi DEBUG: Modified fifth to %d", chord.intervals[2]);
                    continue;
                case 6:
                    semitones = 9 + modifier;
                    post("SheetMidi DEBUG: Processing sixth interval, semitones = %d", semitones);
                    break;
                case 7:
                    semitones = 10 + modifier;
                    post("SheetMidi DEBUG: Processing seventh interval, semitones = %d", semitones);
                    break;
                case 9:
                    semitones = 14 + modifier;
                    post("SheetMidi DEBUG: Processing ninth interval, semitones = %d", semitones);
                    break;
                case 11:
                    semitones = 17 + modifier;
                    post("SheetMidi DEBUG: Processing eleventh interval, semitones = %d", semitones);
                    break;
                case 13:
                    semitones = 21 + modifier;
                    post("SheetMidi DEBUG: Processing thirteenth interval, semitones = %d", semitones);
                    break;
                default:
                    post("SheetMidi DEBUG: Skipping unsupported interval %d", interval);
                    continue;
            }
            
            if (chord.num_intervals >= 12) {
                post("SheetMidi DEBUG: WARNING - Maximum intervals reached (%d), cannot add more", chord.num_intervals);
                continue;
            }
            
            chord.intervals[chord.num_intervals++] = semitones;
            post("SheetMidi DEBUG: Added interval %d at position %d, num_intervals = %d", semitones, chord.num_intervals - 1, chord.num_intervals);
        }
        
        // Skip any whitespace
        while (str[pos] && isspace((unsigned char)str[pos])) {
            post("SheetMidi DEBUG: Skipping trailing whitespace at position %d", pos);
            pos++;
        }
    }
    
    if (loop_guard >= 100) {
        post("SheetMidi DEBUG: WARNING - Loop guard triggered! Possible infinite loop detected");
    }
    
    post("SheetMidi DEBUG: Finished parsing chord. Final state:");
    debug_print_chord("SheetMidi DEBUG", &chord);
    
    return chord;
} 