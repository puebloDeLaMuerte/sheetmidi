#include "include/m_pd.h"
#include <string.h>  // Add this include
#include <ctype.h>  // For isdigit()
#include <stdlib.h>  // For rand()

EXTERN void pd_init(t_pd *x);  // Add this declaration

static t_class *p_sheetmidi_class;
static t_class *p_sheetmidi_proxy_class;  // Class for right inlet proxy

typedef struct _p_sheetmidi_proxy {
    t_pd pd;
    struct _p_sheetmidi *x;
} t_p_sheetmidi_proxy;

// Move this before any struct that uses it
typedef struct _chord_data {
    t_symbol *original;     // Original chord symbol
    int root_offset;        // Semitones from C (0-11)
    int intervals[12];      // Array of intervals in semitones
    int num_intervals;      // Number of intervals used
} t_chord_data;

typedef struct _chord_event {
    t_symbol *chord;     // The chord symbol (like "C", "Dm7", etc.)
    int duration;        // Duration in beats (whole number of beats)
    t_chord_data parsed; // Parsed chord data
} t_chord_event;

typedef struct _p_sheetmidi {
    t_object x_obj;
    t_symbol **chords;      // We'll phase this out with the new system
    int num_chords;         // We'll phase this out with the new system
    t_p_sheetmidi_proxy p;  // Proxy for right inlet
    
    // New members
    t_float time_signature; // For validation during parsing only
    t_chord_event *events;  // Array of chord events
    int num_events;         // Number of events
    int total_duration;     // Total duration in beats
    
    // Playback members
    t_outlet *note_outlet;     // Outlet for current note value
    int current_beat;          // Current playback position in beats
} t_p_sheetmidi;

// Move type definitions to the top, after the includes and before any functions
typedef enum {
    TOKEN_CHORD,    // Any chord symbol (C, Dm7, etc.)
    TOKEN_DOT,      // . (hold)
    TOKEN_BAR,      // | (bar separator)
    TOKEN_ERROR     // Invalid token
} token_type_t;

typedef struct _token {
    token_type_t type;
    t_symbol *value;      // Only used for TOKEN_CHORD
} token_t;

// Forward declarations of all helper functions
static token_t atom_to_token(t_atom *atom);
static void clear_events(t_p_sheetmidi *x);
static void distribute_beats_in_bar(t_chord_event *events, int start_idx, int count, int time_sig);
static int parse_chord_sequence(t_p_sheetmidi *x, int argc, t_atom *argv);
static void print_parsed_sequence(t_p_sheetmidi *x);
static t_chord_data parse_chord_symbol(t_symbol *sym);
void p_sheetmidi_note(t_p_sheetmidi *x);  // Add this
void p_sheetmidi_tick(t_p_sheetmidi *x);  // Add this
void p_sheetmidi_root(t_p_sheetmidi *x);
void p_sheetmidi_third(t_p_sheetmidi *x);
void p_sheetmidi_fifth(t_p_sheetmidi *x);

// Add this to store the last sequence for reparsing
static t_atom *last_sequence = NULL;
static int last_sequence_size = 0;

// Internal function to store chords
void p_sheetmidi_store_chords(t_p_sheetmidi *x, int count, t_symbol **symbols) {
    // Free previous chord storage if it exists
    if (x->chords) {
        freebytes(x->chords, x->num_chords * sizeof(t_symbol *));
    }
    
    // Allocate new storage
    x->num_chords = count;
    x->chords = (t_symbol **)getbytes(count * sizeof(t_symbol *));
    
    // Store the chords
    for (int i = 0; i < count; i++) {
        x->chords[i] = symbols[i];
        post("Stored chord %d: %s", i + 1, x->chords[i]->s_name);
    }
}

void p_sheetmidi_bang(t_p_sheetmidi *x) {
    if (x->num_events == 0) {
        post("SheetMidi: No chord sequence stored");
        return;
    }
    
    post("SheetMidi: Current sequence (%d events, total duration: %d beats, signature: %d/%d):", 
         x->num_events, x->total_duration, (int)x->time_signature);
    for (int i = 0; i < x->num_events; i++) {
        post("  Event %d: %s (%d beats)", 
             i + 1, x->events[i].chord->s_name, x->events[i].duration);
    }
}

// Right inlet handlers (via proxy)
void p_sheetmidi_proxy_symbol(t_p_sheetmidi_proxy *p, t_symbol *s) {
    p_sheetmidi_store_chords(p->x, 1, &s);
}

// Add this helper function to calculate beats in a bar with no dots
static void distribute_beats_in_bar(t_chord_event *events, int start_idx, int count, int time_sig) {
    if (count == 0) return;
    
    // If we can't evenly divide the time signature, each chord gets 1 beat
    // unless modified by dots
    if (time_sig % count != 0) {
        for (int i = 0; i < count; i++) {
            // Only set duration to 1 if it hasn't already been modified by dots
            if (events[start_idx + i].duration == 1) {
                events[start_idx + i].duration = 1;  // Keep at 1 beat
            }
            // Note: we keep dot-modified durations as they are
        }
    } else {
        // Clean division possible - distribute evenly
        int beats_per_chord = time_sig / count;
        for (int i = 0; i < count; i++) {
            events[start_idx + i].duration = beats_per_chord;
        }
    }
}

// Parse the chord sequence and store in events array
static int parse_chord_sequence(t_p_sheetmidi *x, int argc, t_atom *argv) {
    clear_events(x);
    
    // First pass: count how many events we'll need
    int max_events = argc; // Maximum possible events
    x->events = (t_chord_event *)getbytes(max_events * sizeof(t_chord_event));
    
    int current_event = 0;    // Index in events array
    int chords_in_bar = 0;    // Count of chords in current bar
    int bar_start_idx = 0;    // Start index of current bar's chords
    int dot_count = 0;        // Count dots after current chord
    int using_dots = 0;       // Whether current bar uses dot notation
    int saw_bar_marker = 0;   // Whether we've seen any bar markers
    
    for (int i = 0; i < argc; i++) {
        token_t token = atom_to_token(&argv[i]);
        
        switch (token.type) {
            case TOKEN_CHORD:
                x->events[current_event].chord = token.value;
                x->events[current_event].duration = 1; // Start with 1 beat for the chord itself
                x->events[current_event].parsed = parse_chord_symbol(token.value); // Parse the chord
                chords_in_bar++;
                current_event++;
                dot_count = 0; // Reset dot count for new chord
                break;
                
            case TOKEN_DOT:
                if (current_event > 0 && chords_in_bar > 0) {
                    x->events[current_event - 1].duration++;
                    using_dots = 1;
                }
                break;
                
            case TOKEN_BAR:
                saw_bar_marker = 1;
                if (chords_in_bar > 0) {
                    if (!using_dots) {
                        // If no dots were used, distribute beats evenly
                        distribute_beats_in_bar(x->events, bar_start_idx, 
                                             chords_in_bar, x->time_signature);
                    }
                    // Reset for next bar
                    bar_start_idx = current_event;
                    chords_in_bar = 0;
                    using_dots = 0;
                }
                break;
                
            case TOKEN_ERROR:
                post("SheetMidi: Invalid token in sequence");
                clear_events(x);
                return 0;
        }
    }
    
    // Handle last bar or entire sequence if no bar markers were used
    if (chords_in_bar > 0) {
        if (!using_dots) {
            if (saw_bar_marker) {
                // Last bar with bar notation
                distribute_beats_in_bar(x->events, bar_start_idx, 
                                      chords_in_bar, x->time_signature);
            } else {
                // No bar markers used at all - treat entire sequence as one bar
                distribute_beats_in_bar(x->events, 0, current_event, x->time_signature);
            }
        }
    }
    
    x->num_events = current_event;
    
    // Calculate total duration
    x->total_duration = 0;
    for (int i = 0; i < x->num_events; i++) {
        x->total_duration += x->events[i].duration;
    }
    
    return 1;
}

// Add this helper function to print the parsed sequence
static void print_parsed_sequence(t_p_sheetmidi *x) {
    post("SheetMidi: Parsed sequence (%d events, total duration: %d beats, signature: %d/%d):", 
         x->num_events, x->total_duration, (int)x->time_signature, 4);
    for (int i = 0; i < x->num_events; i++) {
        t_chord_event *ev = &x->events[i];
        post("  Event %d: %s (%d beats) - root: %d, intervals:", 
             i + 1, ev->chord->s_name, ev->duration, ev->parsed.root_offset);
        
        // Print intervals in a separate line for clarity
        char intervals[128] = "";
        int pos = 0;
        for (int j = 0; j < ev->parsed.num_intervals; j++) {
            pos += snprintf(intervals + pos, sizeof(intervals) - pos, 
                          "%d%s", 
                          ev->parsed.intervals[j],
                          j < ev->parsed.num_intervals - 1 ? ", " : "");
        }
        post("    %s", intervals);
    }
}

// Update time signature handler
void p_sheetmidi_proxy_anything(t_p_sheetmidi_proxy *p, t_symbol *s, int argc, t_atom *argv) {
    if (s == gensym("time")) {
        if (argc > 0 && argv[0].a_type == A_FLOAT) {
            t_float new_time_sig = atom_getfloat(&argv[0]);
            if (new_time_sig != p->x->time_signature) {
                p->x->time_signature = new_time_sig;
                post("SheetMidi: Time signature set to %d", (int)p->x->time_signature);
                
                // Reparse the last sequence if we have one
                if (last_sequence && last_sequence_size > 0) {
                    parse_chord_sequence(p->x, last_sequence_size, last_sequence);
                    print_parsed_sequence(p->x);
                }
            }
        }
    }
}

// Update proxy list handler to store the sequence
void p_sheetmidi_proxy_list(t_p_sheetmidi_proxy *p, t_symbol *s, int argc, t_atom *argv) {
    // Free previous sequence if it exists
    if (last_sequence) {
        freebytes(last_sequence, last_sequence_size * sizeof(t_atom));
    }
    
    // Store new sequence
    last_sequence_size = argc;
    last_sequence = (t_atom *)getbytes(argc * sizeof(t_atom));
    memcpy(last_sequence, argv, argc * sizeof(t_atom));
    
    if (parse_chord_sequence(p->x, argc, argv)) {
        print_parsed_sequence(p->x);
    }
}

// Main object list handler - explicitly ignore lists
void p_sheetmidi_list(t_p_sheetmidi *x, t_symbol *s, int argc, t_atom *argv) {
    // Do nothing - we don't want to handle lists on the main object
}

// Add these new function declarations before p_sheetmidi_new
static int parse_chord_sequence(t_p_sheetmidi *x, int argc, t_atom *argv);
static void clear_events(t_p_sheetmidi *x);

// Update p_sheetmidi_new to remove the float inlet
void *p_sheetmidi_new(void) {
    t_p_sheetmidi *x = (t_p_sheetmidi *)pd_new(p_sheetmidi_class);
    
    // Initialize proxy for right inlet
    x->p.x = x;
    x->p.pd = p_sheetmidi_proxy_class;
    inlet_new(&x->x_obj, &x->p.pd, 0, 0);
    
    // Initialize chord storage (legacy)
    x->chords = NULL;
    x->num_chords = 0;
    
    // Initialize new members
    x->time_signature = 4;  // Default to 4/4
    x->events = NULL;
    x->num_events = 0;
    x->total_duration = 0;
    
    // Initialize playback
    x->current_beat = 0;
    x->note_outlet = outlet_new(&x->x_obj, &s_float);
    
    post("SheetMidi: new instance created");
    return (void *)x;
}

// Update free function to clean up the stored sequence
void p_sheetmidi_free(t_p_sheetmidi *x) {
    if (x->chords) {
        freebytes(x->chords, x->num_chords * sizeof(t_symbol *));
    }
    if (x->events) {
        freebytes(x->events, x->num_events * sizeof(t_chord_event));
    }
    if (last_sequence) {
        freebytes(last_sequence, last_sequence_size * sizeof(t_atom));
        last_sequence = NULL;
        last_sequence_size = 0;
    }
}

// Add helper function to clear events
static void clear_events(t_p_sheetmidi *x) {
    if (x->events) {
        freebytes(x->events, x->num_events * sizeof(t_chord_event));
        x->events = NULL;
        x->num_events = 0;
        x->total_duration = 0;
    }
}

EXTERN void p_sheetmidi_setup(void) {
    post("DEBUG: Starting setup...");
    
    // Create proxy class for right inlet
    p_sheetmidi_proxy_class = class_new(gensym("p_sheetmidi_proxy"),
        0, 0, sizeof(t_p_sheetmidi_proxy),
        CLASS_PD, 0);
    class_addsymbol(p_sheetmidi_proxy_class, p_sheetmidi_proxy_symbol);
    class_addlist(p_sheetmidi_proxy_class, p_sheetmidi_proxy_list);
    class_addanything(p_sheetmidi_proxy_class, p_sheetmidi_proxy_anything);
    
    // Create main class
    p_sheetmidi_class = class_new(gensym("p_sheetmidi"),
        (t_newmethod)p_sheetmidi_new,
        (t_method)p_sheetmidi_free,
        sizeof(t_p_sheetmidi),
        CLASS_DEFAULT,
        0);

    // Left inlet: only bang and explicitly ignore lists
    class_addbang(p_sheetmidi_class, p_sheetmidi_bang);
    class_addlist(p_sheetmidi_class, p_sheetmidi_list);
    
    // Add message methods
    class_addmethod(p_sheetmidi_class, 
                   (t_method)p_sheetmidi_note, 
                   gensym("note"), 0);
    class_addmethod(p_sheetmidi_class, 
                   (t_method)p_sheetmidi_root, 
                   gensym("root"), 0);
    class_addmethod(p_sheetmidi_class, 
                   (t_method)p_sheetmidi_third, 
                   gensym("third"), 0);
    class_addmethod(p_sheetmidi_class, 
                   (t_method)p_sheetmidi_fifth, 
                   gensym("fifth"), 0);
    class_addmethod(p_sheetmidi_class, 
                   (t_method)p_sheetmidi_tick, 
                   gensym("tick"), 0);
    
    post("SheetMidi: external loaded");
}

// Implementation of atom_to_token that was cut off in the file
static token_t atom_to_token(t_atom *atom) {
    token_t token = {TOKEN_ERROR, NULL};
    
    if (atom->a_type != A_SYMBOL) {
        return token;
    }
    
    t_symbol *sym = atom_getsymbol(atom);
    const char *str = sym->s_name;
    
    if (strcmp(str, ".") == 0) {
        token.type = TOKEN_DOT;
    }
    else if (strcmp(str, "|") == 0) {
        token.type = TOKEN_BAR;
    }
    else {
        token.type = TOKEN_CHORD;
        token.value = sym;
    }
    
    return token;
}

static t_chord_data parse_chord_symbol(t_symbol *sym) {
    t_chord_data chord = {
        .original = sym,
        .root_offset = 0,
        .num_intervals = 0
    };
    memset(chord.intervals, -1, sizeof(chord.intervals));
    
    const char *str = sym->s_name;
    int pos = 0;
    
    // 1. Parse root note (same as before)
    switch(str[pos]) {
        case 'C': chord.root_offset = 0; break;
        case 'D': chord.root_offset = 2; break;
        case 'E': chord.root_offset = 4; break;
        case 'F': chord.root_offset = 5; break;
        case 'G': chord.root_offset = 7; break;
        case 'A': chord.root_offset = 9; break;
        case 'B': chord.root_offset = 11; break;
        default: return chord; // Invalid root
    }
    pos++;
    
    // Root modifier (same as before)
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
    } else if (strncmp(&str[pos], "dim", 3) == 0) {
        third = 3;  // Minor third
        fifth = 6;  // Diminished fifth
        pos += 3;
    }
    chord.intervals[chord.num_intervals++] = third;
    chord.intervals[chord.num_intervals++] = fifth;
    
    // 3. Parse remaining intervals and modifications
    while (str[pos] != '\0') {
        int modifier = 0;
        
        // Handle modifiers
        if (str[pos] == 'b') {
            modifier = -1;
            pos++;
        } else if (str[pos] == '#') {
            modifier = 1;
            pos++;
        } else if (strncmp(&str[pos], "maj", 3) == 0 || str[pos] == 'M') {
            modifier = 1;
            pos += (str[pos] == 'M' ? 1 : 3);
        }
        
        // Parse each interval number separately
        while (isdigit(str[pos])) {
            int interval = str[pos] - '0';
            pos++;
            
            // Handle two-digit intervals only if next char is also not a digit
            if (isdigit(str[pos]) && !isdigit(str[pos + 1])) {
                interval = interval * 10 + (str[pos] - '0');
                pos++;
            }
            
            // Convert interval number to semitones
            int semitones;
            switch(interval) {
                case 5:  // Modify the fifth
                    chord.intervals[2] = 7 + modifier;  // Modify existing fifth
                    continue;
                case 6:  // Special case for 6th
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
                    continue; // Invalid interval
            }
            
            chord.intervals[chord.num_intervals++] = semitones;
        }
    }
    
    return chord;
}

// Helper function to get current event
static t_chord_event* get_current_event(t_p_sheetmidi *x) {
    if (x->num_events == 0) return NULL;
    
    int beat_count = 0;
    int event_idx = 0;
    
    while (beat_count + x->events[event_idx].duration <= x->current_beat) {
        beat_count += x->events[event_idx].duration;
        event_idx++;
        if (event_idx >= x->num_events) {
            x->current_beat = 0;
            beat_count = 0;
            event_idx = 0;
        }
    }
    
    return &x->events[event_idx];
}

// Update note handler to play random interval
void p_sheetmidi_note(t_p_sheetmidi *x) {
    t_chord_event *ev = get_current_event(x);
    if (!ev) return;
    
    // Pick random interval from the available ones
    int random_idx = rand() % ev->parsed.num_intervals;
    t_float note = ev->parsed.root_offset + ev->parsed.intervals[random_idx];
    outlet_float(x->note_outlet, note);
}

// Add new handlers for specific intervals
void p_sheetmidi_root(t_p_sheetmidi *x) {
    t_chord_event *ev = get_current_event(x);
    if (!ev) return;
    
    t_float note = ev->parsed.root_offset + ev->parsed.intervals[0];  // Root is always first
    outlet_float(x->note_outlet, note);
}

void p_sheetmidi_third(t_p_sheetmidi *x) {
    t_chord_event *ev = get_current_event(x);
    if (!ev || ev->parsed.num_intervals < 2) return;
    
    t_float note = ev->parsed.root_offset + ev->parsed.intervals[1];  // Third is second
    outlet_float(x->note_outlet, note);
}

void p_sheetmidi_fifth(t_p_sheetmidi *x) {
    t_chord_event *ev = get_current_event(x);
    if (!ev || ev->parsed.num_intervals < 3) return;
    
    t_float note = ev->parsed.root_offset + ev->parsed.intervals[2];  // Fifth is third
    outlet_float(x->note_outlet, note);
}

void p_sheetmidi_tick(t_p_sheetmidi *x) {
    if (x->num_events == 0) return;
    
    // Advance beat counter
    x->current_beat++;
    
    // Check for wraparound
    if (x->current_beat >= x->total_duration) {
        x->current_beat = 0;
    }
}


