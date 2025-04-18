#include "m_pd.h"
#include "chord_data.h"
#include "p_sheetmidi_types.h"
#include "post_utils.h"
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

void debug_print_chord(const char* prefix, const t_chord_data* chord) {
    info_post("%s: Root: %d, Intervals:", prefix, chord->root_offset);
    for (int i = 0; i < chord->num_intervals; i++) {
        info_post("  %d", chord->intervals[i]);
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
    
    // Note: We can't use debug_post here since we don't have access to the t_p_sheetmidi instance
    // This is a limitation we'll have to live with for this function
    
    // Skip leading whitespace and non-printable characters
    while (*str && (!isprint((unsigned char)*str) || isspace((unsigned char)*str))) {
        str++;
    }
    
    // If string is empty after trimming
    if (!*str) {
        return chord;
    }
    
    int pos = 0;
    
    // 1. Parse root note
    if (!isprint((unsigned char)str[pos])) {
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
            return chord;
    }
    pos++;
    
    // Root modifier
    if (str[pos] == 'b') {
        chord.root_offset = (chord.root_offset - 1 + 12) % 12;
        pos++;
    } else if (str[pos] == '#') {
        chord.root_offset = (chord.root_offset + 1) % 12;
        pos++;
    }
    
    // Always add root note
    chord.intervals[chord.num_intervals++] = 0;
    
    // 2. Check for minor indicator
    int third = 4;  // Default to major third
    int fifth = 7;  // Default to perfect fifth
    if (str[pos] == 'm') {
        third = 3;  // Minor third
        pos++;
        // Check for 'min' or 'mi'
        if (strncmp(&str[pos], "in", 2) == 0) {
            pos += 2;
        } else if (str[pos] == 'i') {
            pos++;
        } else if (str[pos] == 'a' && str[pos + 1] == 'j') {
            // If we see 'maj', this is actually not a minor chord
            third = 4;  // Revert to major third
            pos--;  // Go back to 'm' to let the maj parsing handle it
        }
    } else if (str[pos] == 'M' && str[pos + 1] == 'I') {
        third = 3;  // Minor third
        pos += 2;
    } else if (strncmp(&str[pos], "dim", 3) == 0) {
        third = 3;  // Minor third
        fifth = 6;  // Diminished fifth
        pos += 3;
    }
    chord.intervals[chord.num_intervals++] = third;
    chord.intervals[chord.num_intervals++] = fifth;
    
    // 3. Parse remaining intervals and modifications
    int loop_guard = 0;  // Safety counter to prevent infinite loops
    while (str[pos] != '\0' && loop_guard < 100) {  // Add reasonable maximum iterations
        loop_guard++;
        
        // Skip any whitespace
        while (str[pos] && isspace((unsigned char)str[pos])) {
            pos++;
        }
        if (!str[pos]) break;
        
        int modifier = 0;
        
        if (str[pos] == 'b') {
            modifier = -1;
            pos++;
        } else if (str[pos] == '#') {
            modifier = 1;
            pos++;
        } else if (strncmp(&str[pos], "maj", 3) == 0 || strncmp(&str[pos], "MAJ", 3) == 0 || 
                   strncmp(&str[pos], "Maj", 3) == 0) {
            modifier = 1;
            pos += 3;
        } else if (strncmp(&str[pos], "MA", 2) == 0) {
            modifier = 1;
            pos += 2;
        }
        
        while (isdigit(str[pos])) {
            int interval = str[pos] - '0';
            pos++;
            
            if (isdigit(str[pos]) && !isdigit(str[pos + 1])) {
                interval = interval * 10 + (str[pos] - '0');
                pos++;
            }
            
            int semitones;
            switch(interval) {
                case 5:
                    chord.intervals[2] = 7 + modifier;
                    continue;
                case 6:
                    semitones = 9 + modifier;
                    break;
                case 7:
                    semitones = 10 + modifier;
                    break;
                case 9:
                    semitones = 14 + modifier;
                    break;
                case 11:
                    semitones = 17 + modifier;
                    break;
                case 13:
                    semitones = 21 + modifier;
                    break;
                default:
                    continue;
            }
            
            if (chord.num_intervals >= 12) {
                continue;
            }
            
            chord.intervals[chord.num_intervals++] = semitones;
        }
        
        // Skip any whitespace
        while (str[pos] && isspace((unsigned char)str[pos])) {
            pos++;
        }
    }
    
    return chord;
} 