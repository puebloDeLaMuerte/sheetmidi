#include "include/m_pd.h"
#include <string.h>  // Add this include

EXTERN void pd_init(t_pd *x);  // Add this declaration

static t_class *p_sheetmidi_class;
static t_class *p_sheetmidi_proxy_class;  // Class for right inlet proxy

typedef struct _p_sheetmidi_proxy {
    t_pd pd;
    struct _p_sheetmidi *x;
} t_p_sheetmidi_proxy;

typedef struct _chord_event {
    t_symbol *chord;     // The chord symbol (like "C", "Dm7", etc.)
    int duration;        // Duration in beats (whole number of beats)
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
        post("  Event %d: %s (%d beats)", 
             i + 1, x->events[i].chord->s_name, x->events[i].duration);
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


