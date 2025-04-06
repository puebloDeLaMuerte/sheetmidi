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
    t_outlet *debug_outlet;    // Outlet for chord symbols
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
static int tokenize_string(t_p_sheetmidi *x, const char *str, t_atom **tokens, int *num_tokens);
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

// Update proxy class to handle both symbol and list input
void p_sheetmidi_proxy_anything(t_p_sheetmidi_proxy *p, t_symbol *s, int argc, t_atom *argv) {
    if (!p || !p->x) return;

    // Handle time signature changes
    if (s == gensym("time")) {
        if (argc > 0 && argv[0].a_type == A_FLOAT) {
            t_float new_time_sig = atom_getfloat(&argv[0]);
            if (new_time_sig != p->x->time_signature) {
                p->x->time_signature = new_time_sig;
                post("SheetMidi: Time signature set to %d", (int)p->x->time_signature);
                
                // Reparse last sequence if we have one
                if (last_sequence && last_sequence_size > 0) {
                    parse_chord_sequence(p->x, last_sequence_size, last_sequence);
                    print_parsed_sequence(p->x);
                }
            }
        }
        return;
    }

    // For all other messages, build a complete string from selector and args
    int buffer_size = 1024;  // Initial buffer size
    char *combined = (char *)getbytes(buffer_size);
    if (!combined) {
        post("SheetMidi: Failed to allocate memory for input string");
        return;
    }
    
    // Start with the selector (unless it's "list")
    int pos = 0;
    if (s != gensym("list")) {
        strncpy(combined, s->s_name, buffer_size - 1);
        pos = strlen(combined);
        if (pos < buffer_size - 1) {
            combined[pos++] = ' ';
        }
    }

    // Add all arguments
    for (int i = 0; i < argc && pos < buffer_size - 1; i++) {
        // Add space between tokens
        if (pos > 0 && combined[pos-1] != ' ') {
            combined[pos++] = ' ';
        }

        // Convert atom to string and append
        if (argv[i].a_type == A_SYMBOL) {
            t_symbol *sym = atom_getsymbol(&argv[i]);
            int len = strlen(sym->s_name);
            if (pos + len < buffer_size - 1) {
                strcpy(combined + pos, sym->s_name);
                pos += len;
            }
        }
        else if (argv[i].a_type == A_FLOAT) {
            char numstr[32];
            snprintf(numstr, sizeof(numstr), "%.g", atom_getfloat(&argv[i]));
            int len = strlen(numstr);
            if (pos + len < buffer_size - 1) {
                strcpy(combined + pos, numstr);
                pos += len;
            }
        }
    }
    combined[pos] = '\0';

    post("SheetMidi: Processing input string: '%s'", combined);
    
    // Parse the combined string into tokens
    t_atom *tokens = NULL;
    int num_tokens = 0;
    if (!tokenize_string(p->x, combined, &tokens, &num_tokens)) {
        post("SheetMidi: Failed to tokenize input string");
        freebytes(combined, buffer_size);
        return;
    }

    // Store the tokens for potential reparsing
    if (last_sequence) {
        freebytes(last_sequence, last_sequence_size * sizeof(t_atom));
    }
    last_sequence = tokens;
    last_sequence_size = num_tokens;

    // Parse the sequence
    if (parse_chord_sequence(p->x, num_tokens, tokens)) {
        print_parsed_sequence(p->x);
    }

    freebytes(combined, buffer_size);
}

// Helper function to tokenize a string into atoms
static int tokenize_string(t_p_sheetmidi *x, const char *str, t_atom **tokens, int *num_tokens) {
    if (!str || !tokens || !num_tokens) return 0;
    
    // First pass: count tokens
    int max_tokens = 0;
    const char *p = str;
    int in_token = 0;
    
    while (*p) {
        if (*p == '|') {
            if (in_token) max_tokens++;
            max_tokens++; // Count the bar itself
            in_token = 0;
        } else if (isspace(*p)) {
            if (in_token) max_tokens++;
            in_token = 0;
        } else if (!in_token) {
            in_token = 1;
        }
        p++;
    }
    if (in_token) max_tokens++;
    
    // Allocate token array
    *tokens = (t_atom *)getbytes(max_tokens * sizeof(t_atom));
    if (!*tokens) return 0;
    *num_tokens = 0;
    
    // Second pass: create tokens
    p = str;
    char token[256] = {0};
    int token_len = 0;
    
    while (*p) {
        if (*p == '|') {
            // Output any pending token
            if (token_len > 0) {
                // Trim trailing whitespace
                while (token_len > 0 && isspace((unsigned char)token[token_len - 1])) {
                    token_len--;
                }
                token[token_len] = '\0';
                
                // Skip leading whitespace
                char *start = token;
                while (*start && isspace((unsigned char)*start)) start++;
                
                if (*start) {  // Only output non-empty tokens
                    // Create a clean copy of the token
                    char *clean = (char *)getbytes(strlen(start) + 1);
                    strcpy(clean, start);
                    t_symbol *sym = gensym(clean);
                    freebytes(clean, strlen(clean) + 1);
                    
                    SETSYMBOL(&(*tokens)[*num_tokens], sym);
                    (*num_tokens)++;
                }
                token_len = 0;
            }
            
            // Output the bar marker
            SETSYMBOL(&(*tokens)[*num_tokens], gensym("|"));
            (*num_tokens)++;
        } else if (isspace((unsigned char)*p)) {
            // Output token on whitespace if we have one
            if (token_len > 0) {
                // Trim trailing whitespace
                while (token_len > 0 && isspace((unsigned char)token[token_len - 1])) {
                    token_len--;
                }
                token[token_len] = '\0';
                
                // Skip leading whitespace
                char *start = token;
                while (*start && isspace((unsigned char)*start)) start++;
                
                if (*start) {  // Only output non-empty tokens
                    // Create a clean copy of the token
                    char *clean = (char *)getbytes(strlen(start) + 1);
                    strcpy(clean, start);
                    t_symbol *sym = gensym(clean);
                    freebytes(clean, strlen(clean) + 1);
                    
                    SETSYMBOL(&(*tokens)[*num_tokens], sym);
                    (*num_tokens)++;
                }
                token_len = 0;
            }
        } else {
            // Add to current token if it's a valid character
            if (token_len < sizeof(token) - 1 && isprint((unsigned char)*p)) {
                token[token_len++] = *p;
            }
        }
        p++;
    }
    
    // Output any final token
    if (token_len > 0) {
        // Trim trailing whitespace
        while (token_len > 0 && isspace((unsigned char)token[token_len - 1])) {
            token_len--;
        }
        token[token_len] = '\0';
        
        // Skip leading whitespace
        char *start = token;
        while (*start && isspace((unsigned char)*start)) start++;
        
        if (*start) {  // Only output non-empty tokens
            // Create a clean copy of the token
            char *clean = (char *)getbytes(strlen(start) + 1);
            strcpy(clean, start);
            t_symbol *sym = gensym(clean);
            freebytes(clean, strlen(clean) + 1);
            
            SETSYMBOL(&(*tokens)[*num_tokens], sym);
            (*num_tokens)++;
        }
    }
    
    return 1;
}

// Helper function to check if a character is valid ASCII
static int is_valid_ascii(char c) {
    return (c >= 0 && c <= 127);
}

// Helper function to trim whitespace and create a symbol
static t_symbol* create_trimmed_symbol(const char* str) {
    if (!str) return NULL;
    
    // Skip leading whitespace
    while (*str && isspace(*str)) str++;
    
    // If string is empty after trimming leading space
    if (!*str) return NULL;
    
    // Find end of string
    const char* end = str + strlen(str) - 1;
    
    // Count trailing whitespace
    while (end > str && isspace(*end)) end--;
    
    // Calculate length of trimmed string
    int len = end - str + 1;
    
    // Create temporary buffer
    char* trimmed = (char*)getbytes(len + 1);
    if (!trimmed) return NULL;
    
    // Copy trimmed portion
    strncpy(trimmed, str, len);
    trimmed[len] = '\0';
    
    // Create symbol and free buffer
    t_symbol* sym = gensym(trimmed);
    freebytes(trimmed, len + 1);
    
    return sym;
}

// Function to sanitize input atoms
static int sanitize_input_atoms(t_p_sheetmidi *x, int argc, t_atom *argv, t_atom **sanitized_atoms, int *sanitized_count) {
    if (!x || !argv || !sanitized_atoms || !sanitized_count || argc <= 0) {
        post("SheetMidi: Invalid input parameters to sanitize_input_atoms");
        return 0;
    }
    
    // First, calculate total length needed for the combined string
    int total_len = 0;
    for (int i = 0; i < argc; i++) {
        if (argv[i].a_type != A_SYMBOL) {
            post("SheetMidi: Invalid input: atom at position %d is not a symbol", i);
            return 0;
        }
        t_symbol *sym = atom_getsymbol(&argv[i]);
        if (!sym || !sym->s_name) {
            post("SheetMidi: Invalid input: null symbol at position %d", i);
            return 0;
        }
        total_len += strlen(sym->s_name) + 1; // +1 for space between tokens
    }
    
    // Allocate buffer for combined string
    char *combined = (char *)getbytes(total_len + 1);
    if (!combined) {
        post("SheetMidi: Failed to allocate memory for combined string");
        return 0;
    }
    combined[0] = '\0';
    
    // Combine all atoms into one string
    for (int i = 0; i < argc; i++) {
        t_symbol *sym = atom_getsymbol(&argv[i]);
        strcat(combined, sym->s_name);
        if (i < argc - 1) strcat(combined, " ");
    }
    
    post("SheetMidi: Combined input: '%s'", combined);
    
    // First pass: count tokens
    int max_tokens = 0;
    const char *p = combined;
    int in_token = 0;
    
    while (*p) {
        if (*p == '|') {
            if (in_token) max_tokens++;
            max_tokens++; // Count the bar itself
            in_token = 0;
        } else if (isspace(*p)) {
            if (in_token) max_tokens++;
            in_token = 0;
        } else if (!in_token) {
            in_token = 1;
        }
        p++;
    }
    if (in_token) max_tokens++;
    
    // Allocate sanitized atoms array
    *sanitized_atoms = (t_atom *)getbytes(max_tokens * sizeof(t_atom));
    if (!*sanitized_atoms) {
        post("SheetMidi: Failed to allocate memory for sanitized atoms");
        freebytes(combined, total_len + 1);
        return 0;
    }
    
    // Second pass: create tokens
    *sanitized_count = 0;
    p = combined;
    char token[256] = {0};
    int token_len = 0;
    
    while (*p) {
        if (*p == '|') {
            // Output any pending token
            if (token_len > 0) {
                // Trim trailing whitespace
                while (token_len > 0 && isspace(token[token_len - 1])) {
                    token_len--;
                }
                token[token_len] = '\0';
                
                // Skip leading whitespace
                char *start = token;
                while (*start && isspace(*start)) start++;
                
                if (*start) {
                    post("SheetMidi: Creating symbol from token '%s'", start);
                    SETSYMBOL(&(*sanitized_atoms)[*sanitized_count], gensym(start));
                    (*sanitized_count)++;
                }
                token_len = 0;
            }
            
            // Output the bar marker
            post("SheetMidi: Found bar marker");
            SETSYMBOL(&(*sanitized_atoms)[*sanitized_count], gensym("|"));
            (*sanitized_count)++;
        } else if (isspace(*p)) {
            // Output token on whitespace if we have one
            if (token_len > 0) {
                // Trim trailing whitespace
                while (token_len > 0 && isspace(token[token_len - 1])) {
                    token_len--;
                }
                token[token_len] = '\0';
                
                // Skip leading whitespace
                char *start = token;
                while (*start && isspace(*start)) start++;
                
                if (*start) {
                    post("SheetMidi: Creating symbol from token '%s'", start);
                    SETSYMBOL(&(*sanitized_atoms)[*sanitized_count], gensym(start));
                    (*sanitized_count)++;
                }
                token_len = 0;
            }
        } else {
            // Add to current token
            if (token_len < sizeof(token) - 1) {
                token[token_len++] = *p;
            }
        }
        p++;
    }
    
    // Output any final token
    if (token_len > 0) {
        // Trim trailing whitespace
        while (token_len > 0 && isspace(token[token_len - 1])) {
            token_len--;
        }
        token[token_len] = '\0';
        
        // Skip leading whitespace
        char *start = token;
        while (*start && isspace(*start)) start++;
        
        if (*start) {
            post("SheetMidi: Creating symbol from token '%s'", start);
            SETSYMBOL(&(*sanitized_atoms)[*sanitized_count], gensym(start));
            (*sanitized_count)++;
        }
    }
    
    // Free combined string
    freebytes(combined, total_len + 1);
    
    // Debug output
    post("SheetMidi: Sanitization complete, %d atoms processed", *sanitized_count);
    for (int i = 0; i < *sanitized_count; i++) {
        t_symbol *sym = atom_getsymbol(&(*sanitized_atoms)[i]);
        post("  Sanitized atom %d: '%s'", i, sym->s_name);
    }
    
    return 1;
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
    x->debug_outlet = outlet_new(&x->x_obj, &s_symbol);  // For chord symbols
    
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

// Update setup function to handle both symbol and list input
EXTERN void p_sheetmidi_setup(void) {
    post("DEBUG: Starting setup...");
    
    // Create proxy class for right inlet
    p_sheetmidi_proxy_class = class_new(gensym("p_sheetmidi_proxy"),
        0, 0, sizeof(t_p_sheetmidi_proxy),
        CLASS_PD, 0);
    
    // Only register the anything method
    class_addanything(p_sheetmidi_proxy_class, p_sheetmidi_proxy_anything);
    
    // Create main class
    p_sheetmidi_class = class_new(gensym("p_sheetmidi"),
        (t_newmethod)p_sheetmidi_new,
        (t_method)p_sheetmidi_free,
        sizeof(t_p_sheetmidi),
        CLASS_DEFAULT,
        0);
    
    // Left inlet: only bang
    class_addbang(p_sheetmidi_class, p_sheetmidi_bang);
    
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
    
    post("SheetMidi: external loaded - use symbol or list messages for chord sequences");
}

// Implementation of atom_to_token that was cut off in the file
static token_t atom_to_token(t_atom *atom) {
    token_t token = {TOKEN_ERROR, NULL};
    
    if (atom->a_type != A_SYMBOL) {
        post("  atom_to_token: Got non-symbol atom");
        return token;
    }
    
    t_symbol *sym = atom_getsymbol(atom);
    const char *str = sym->s_name;
    post("  atom_to_token: Processing symbol '%s'", str);
    
    if (strcmp(str, ".") == 0) {
        token.type = TOKEN_DOT;
        post("    -> Interpreted as DOT token");
    }
    else if (strcmp(str, "|") == 0) {
        token.type = TOKEN_BAR;
        post("    -> Interpreted as BAR token");
    }
    else {
        token.type = TOKEN_CHORD;
        token.value = sym;
        post("    -> Interpreted as CHORD token");
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
    
    // Skip leading whitespace more aggressively
    while (*str && isspace((unsigned char)*str)) str++;
    
    // If string is empty after trimming
    if (!*str) {
        post("SheetMidi: Empty chord symbol after trimming whitespace");
        return chord;
    }
    
    // Debug output
    post("SheetMidi: Parsing chord '%s' (trimmed from '%s')", str, sym->s_name);
    
    int pos = 0;
    
    // 1. Parse root note
    switch(str[pos]) {
        case 'C': chord.root_offset = 0; break;
        case 'D': chord.root_offset = 2; break;
        case 'E': chord.root_offset = 4; break;
        case 'F': chord.root_offset = 5; break;
        case 'G': chord.root_offset = 7; break;
        case 'A': chord.root_offset = 9; break;
        case 'B': chord.root_offset = 11; break;
        default: 
            post("SheetMidi: Invalid root note '%c' (ASCII: %d) in chord '%s'", 
                 str[pos], (int)(unsigned char)str[pos], str);
            return chord; // Invalid root
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

// Helper function to output debug info
static void output_debug_chord(t_p_sheetmidi *x, t_chord_event *ev) {
    if (ev && ev->chord) {
        outlet_symbol(x->debug_outlet, ev->chord);
    }
}

// Update note handler to play random interval
void p_sheetmidi_note(t_p_sheetmidi *x) {
    t_chord_event *ev = get_current_event(x);
    if (!ev) return;
    
    output_debug_chord(x, ev);  // Output chord symbol first
    
    // Safety check to prevent division by zero
    if (ev->parsed.num_intervals <= 0) {
        // If no intervals, just output the root note
        t_float note = ev->parsed.root_offset;
        outlet_float(x->note_outlet, note);
        return;
    }
    
    int random_idx = rand() % ev->parsed.num_intervals;
    t_float note = ev->parsed.root_offset + ev->parsed.intervals[random_idx];
    outlet_float(x->note_outlet, note);
}

// Add new handlers for specific intervals
void p_sheetmidi_root(t_p_sheetmidi *x) {
    t_chord_event *ev = get_current_event(x);
    if (!ev) return;
    
    output_debug_chord(x, ev);  // Output chord symbol first
    
    t_float note = ev->parsed.root_offset + ev->parsed.intervals[0];
    outlet_float(x->note_outlet, note);
}

void p_sheetmidi_third(t_p_sheetmidi *x) {
    t_chord_event *ev = get_current_event(x);
    if (!ev || ev->parsed.num_intervals < 2) return;
    
    output_debug_chord(x, ev);  // Output chord symbol first
    
    t_float note = ev->parsed.root_offset + ev->parsed.intervals[1];
    outlet_float(x->note_outlet, note);
}

void p_sheetmidi_fifth(t_p_sheetmidi *x) {
    t_chord_event *ev = get_current_event(x);
    if (!ev || ev->parsed.num_intervals < 3) return;
    
    output_debug_chord(x, ev);  // Output chord symbol first
    
    t_float note = ev->parsed.root_offset + ev->parsed.intervals[2];
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

// Helper function to distribute beats evenly in a bar
static void distribute_beats_in_bar(t_chord_event *events, int start_idx, int count, int time_sig) {
    if (count == 0) return;
    
    // Calculate beats per chord (integer division)
    int beats_per_chord = time_sig / count;
    int extra_beats = time_sig % count;
    
    // Distribute beats
    for (int i = 0; i < count; i++) {
        events[start_idx + i].duration = beats_per_chord;
        if (i < extra_beats) {
            events[start_idx + i].duration++;
        }
    }
}

// Parse a sequence of chord tokens into events
static int parse_chord_sequence(t_p_sheetmidi *x, int argc, t_atom *argv) {
    if (!x || !argv || argc <= 0) return 0;
    
    // Clear existing events
    clear_events(x);
    
    // First pass: count events (chords and dots, excluding bars)
    int num_events = 0;
    int current_bar_start = 0;
    int chords_in_current_bar = 0;
    
    for (int i = 0; i < argc; i++) {
        token_t token = atom_to_token(&argv[i]);
        post("  parse_chord_sequence: Token %d type=%d", i, token.type);
        
        switch (token.type) {
            case TOKEN_CHORD:
            case TOKEN_DOT:
                num_events++;
                chords_in_current_bar++;
                break;
            case TOKEN_BAR:
                // Distribute beats in the current bar
                if (chords_in_current_bar > 0) {
                    chords_in_current_bar = 0;
                    current_bar_start = num_events;
                }
                break;
            case TOKEN_ERROR:
                post("SheetMidi: Error parsing token at position %d", i);
                return 0;
        }
    }
    
    // Allocate events array
    x->events = (t_chord_event *)getbytes(num_events * sizeof(t_chord_event));
    if (!x->events) {
        post("SheetMidi: Failed to allocate memory for events");
        return 0;
    }
    x->num_events = num_events;
    
    // Second pass: create events
    int event_idx = 0;
    current_bar_start = 0;
    chords_in_current_bar = 0;
    t_symbol *last_chord = NULL;
    
    for (int i = 0; i < argc; i++) {
        token_t token = atom_to_token(&argv[i]);
        
        switch (token.type) {
            case TOKEN_CHORD:
                x->events[event_idx].chord = token.value;
                x->events[event_idx].parsed = parse_chord_symbol(token.value);
                last_chord = token.value;
                chords_in_current_bar++;
                event_idx++;
                break;
                
            case TOKEN_DOT:
                if (!last_chord) {
                    post("SheetMidi: Dot without preceding chord");
                    clear_events(x);
                    return 0;
                }
                x->events[event_idx].chord = last_chord;
                x->events[event_idx].parsed = parse_chord_symbol(last_chord);
                chords_in_current_bar++;
                event_idx++;
                break;
                
            case TOKEN_BAR:
                // Distribute beats in the current bar
                if (chords_in_current_bar > 0) {
                    distribute_beats_in_bar(x->events, current_bar_start, 
                                         chords_in_current_bar, x->time_signature);
                    current_bar_start = event_idx;
                    chords_in_current_bar = 0;
                }
                break;
                
            case TOKEN_ERROR:
                clear_events(x);
                return 0;
        }
    }
    
    // Handle last bar if it wasn't terminated
    if (chords_in_current_bar > 0) {
        distribute_beats_in_bar(x->events, current_bar_start, 
                              chords_in_current_bar, x->time_signature);
    }
    
    // Calculate total duration
    x->total_duration = 0;
    for (int i = 0; i < x->num_events; i++) {
        x->total_duration += x->events[i].duration;
    }
    
    return 1;
}

// Print the parsed sequence for debugging
static void print_parsed_sequence(t_p_sheetmidi *x) {
    if (!x || !x->events || x->num_events == 0) {
        post("SheetMidi: No sequence to print");
        return;
    }
    
    post("SheetMidi: Parsed sequence (%d events, total duration: %d beats):", 
         x->num_events, x->total_duration);
    
    int current_bar = 0;
    int beats_in_bar = 0;
    
    for (int i = 0; i < x->num_events; i++) {
        t_chord_event *ev = &x->events[i];
        
        // Check if we need to start a new bar
        if (beats_in_bar + ev->duration > x->time_signature) {
            current_bar++;
            beats_in_bar = 0;
            post("  |");  // Print bar line
        }
        
        // Print event info
        post("    Event %d (bar %d, beat %d): %s (%d beats)", 
             i + 1, current_bar + 1, beats_in_bar + 1,
             ev->chord->s_name, ev->duration);
        
        // Print parsed chord data
        post("      Root: %d, Intervals:", ev->parsed.root_offset);
        for (int j = 0; j < ev->parsed.num_intervals; j++) {
            post("        %d", ev->parsed.intervals[j]);
        }
        
        beats_in_bar += ev->duration;
    }
}


