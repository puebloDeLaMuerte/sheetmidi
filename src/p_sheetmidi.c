#include "m_pd.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdarg.h>
#include "p_sheetmidi.h"
#include "chord_data.h"
#include "token_handler.h"
#include "post_utils.h"

EXTERN void pd_init(t_pd *x);

static t_class *p_sheetmidi_class;
static t_class *p_sheetmidi_proxy_class;

// Forward declarations of internal helper functions
static void clear_events(t_p_sheetmidi *x);
static void distribute_beats_in_bar(t_chord_event *events, int start_idx, int count, int time_sig);
static int parse_chord_sequence(t_p_sheetmidi *x, int argc, t_atom *argv);
static void print_parsed_sequence(t_p_sheetmidi *x);
static t_chord_event* get_current_event(t_p_sheetmidi *x);
static void output_debug_chord(t_p_sheetmidi *x, t_chord_event *ev);
void p_sheetmidi_note(t_p_sheetmidi *x);
void p_sheetmidi_tick(t_p_sheetmidi *x);
void p_sheetmidi_root(t_p_sheetmidi *x);
void p_sheetmidi_third(t_p_sheetmidi *x);
void p_sheetmidi_fifth(t_p_sheetmidi *x);

// Add this to store the last sequence for reparsing
static t_atom *last_sequence = NULL;
static int last_sequence_size = 0;

void p_sheetmidi_bang(t_p_sheetmidi *x) {
    if (x->num_events == 0) {
        info_post("SheetMidi: No chord sequence stored");
        return;
    }
    print_parsed_sequence(x);
}

// Add function to output beat position
static void output_beat_position(t_p_sheetmidi *x) {
    outlet_float(x->beat_outlet, x->current_beat);
}

// Add function to handle beat resetting
static void reset_beat(t_p_sheetmidi *x, t_float new_beat) {
    if (x->total_duration > 0) {
        // Wrap around using modulo
        x->current_beat = ((int)new_beat % x->total_duration + x->total_duration) % x->total_duration;
        debug_post(x, "SheetMidi DEBUG: Beat reset to %d", x->current_beat);
        output_debug_chord(x, get_current_event(x));
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
                info_post("SheetMidi: Time signature set to %d", (int)p->x->time_signature);
                
                // Reparse last sequence if we have one
                if (last_sequence && last_sequence_size > 0) {
                    parse_chord_sequence(p->x, last_sequence_size, last_sequence);
                    print_parsed_sequence(p->x);
                }
            }
        }
        return;
    }

    // Handle beat reset
    if (s == gensym("beat")) {
        if (argc > 0 && argv[0].a_type == A_FLOAT) {
            reset_beat(p->x, atom_getfloat(&argv[0]));
        }
        return;
    }

    // For all other messages, tokenize and parse
    token_t *tokens = NULL;
    int num_tokens = 0;
    
    // Build complete string from selector and args
    int buffer_size = 1024;
    char *combined = (char *)getbytes(buffer_size);
    if (!combined) {
        info_post("SheetMidi: Failed to allocate memory for input string");
        return;
    }
    
    // Start with selector
    strncpy(combined, s->s_name, buffer_size - 1);
    int pos = strlen(combined);
    
    // Add arguments
    for (int i = 0; i < argc && pos < buffer_size - 2; i++) {  // -2 to ensure space for space and null
        combined[pos++] = ' ';  // Always add a space before each argument
        if (argv[i].a_type == A_SYMBOL) {
            t_symbol *sym = atom_getsymbol(&argv[i]);
            int len = strlen(sym->s_name);
            if (pos + len < buffer_size - 1) {
                strcpy(combined + pos, sym->s_name);
                pos += len;
            }
        }
    }
    combined[pos] = '\0';
    
    // First convert string to tokens
    if (tokenize_string(p->x, combined, &tokens, &num_tokens)) {
        // Convert tokens to atoms for storage
        if (last_sequence) {
            freebytes(last_sequence, last_sequence_size * sizeof(t_atom));
        }
        last_sequence = (t_atom *)getbytes(num_tokens * sizeof(t_atom));
        last_sequence_size = num_tokens;
        
        for (int i = 0; i < num_tokens; i++) {
            if (tokens[i].type == TOKEN_CHORD) {
                SETSYMBOL(&last_sequence[i], tokens[i].value);
            } else if (tokens[i].type == TOKEN_BAR) {
                SETSYMBOL(&last_sequence[i], gensym("|"));
            } else if (tokens[i].type == TOKEN_DOT) {
                SETSYMBOL(&last_sequence[i], gensym("."));
            }
        }

        if (parse_chord_sequence(p->x, num_tokens, last_sequence)) {
            print_parsed_sequence(p->x);
        }
        
        freebytes(tokens, num_tokens * sizeof(token_t));
    }
    
    freebytes(combined, buffer_size);
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

// Helper function to clear events
static void clear_events(t_p_sheetmidi *x) {
    if (x->events) {
        freebytes(x->events, x->num_events * sizeof(t_chord_event));
        x->events = NULL;
        x->num_events = 0;
        x->total_duration = 0;
    }
}

// Helper function to distribute beats in a bar
static void distribute_beats_in_bar(t_chord_event *events, int start_idx, int count, int time_sig) {
    if (count == 0) return;
    
    int beats_per_chord = time_sig / count;
    int extra_beats = time_sig % count;
    
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
    
    clear_events(x);
    
    // First pass: count actual events (chords only, not dots)
    int num_events = 0;
    for (int i = 0; i < argc; i++) {
        token_t token = atom_to_token(&argv[i]);
        if (token.type == TOKEN_CHORD) {
            num_events++;
        }
    }
    
    // Allocate events array
    x->events = (t_chord_event *)getbytes(num_events * sizeof(t_chord_event));
    if (!x->events) {
        info_post("SheetMidi: Failed to allocate memory for events");
        return 0;
    }
    x->num_events = num_events;
    
    // Second pass: create events and handle dots
    int event_idx = 0;
    int current_bar_start = 0;
    int chords_in_current_bar = 0;
    int current_chord_dots = 0;  // Dots for current chord being processed
    int bar_has_dots = 0;       // Whether current bar uses dot notation
    t_symbol *last_chord = NULL;
    
    debug_post(x, "SheetMidi DEBUG: Starting second pass parsing");
    
    for (int i = 0; i < argc; i++) {
        token_t token = atom_to_token(&argv[i]);
        
        switch (token.type) {
            case TOKEN_CHORD:
                // If we had a previous chord in this bar, finalize its duration
                if (last_chord && chords_in_current_bar > 0) {
                    if (bar_has_dots) {
                        x->events[event_idx - 1].duration = 1 + current_chord_dots;
                        debug_post(x, "SheetMidi DEBUG: Set previous chord duration to %d (1 + %d dots)", 
                             x->events[event_idx - 1].duration, current_chord_dots);
                    }
                }
                
                // Add new chord event
                x->events[event_idx].chord = token.value;
                x->events[event_idx].parsed = parse_chord_symbol(token.value);
                x->events[event_idx].duration = 1;  // Default duration, may be modified later
                last_chord = token.value;
                current_chord_dots = 0;  // Reset dot count for new chord
                chords_in_current_bar++;
                event_idx++;
                debug_post(x, "SheetMidi DEBUG: Added chord %s at index %d", token.value->s_name, event_idx - 1);
                break;
                
            case TOKEN_DOT:
                if (!last_chord) {
                    info_post("SheetMidi: Dot without preceding chord");
                    clear_events(x);
                    return 0;
                }
                current_chord_dots++;
                bar_has_dots = 1;
                debug_post(x, "SheetMidi DEBUG: Added dot to chord %s, dot count now %d", 
                     last_chord->s_name, current_chord_dots);
                break;
                
            case TOKEN_BAR:
                if (chords_in_current_bar > 0) {
                    // Finalize last chord in bar
                    if (bar_has_dots) {
                        x->events[event_idx - 1].duration = 1 + current_chord_dots;
                        debug_post(x, "SheetMidi DEBUG: Bar with dots - final chord duration %d", 
                             x->events[event_idx - 1].duration);
                    } else {
                        debug_post(x, "SheetMidi DEBUG: Bar without dots - distributing beats among %d chords", 
                             chords_in_current_bar);
                        distribute_beats_in_bar(x->events, current_bar_start, 
                                             chords_in_current_bar, x->time_signature);
                    }
                    
                    // Reset for next bar
                    current_bar_start = event_idx;
                    chords_in_current_bar = 0;
                    current_chord_dots = 0;
                    bar_has_dots = 0;
                    debug_post(x, "SheetMidi DEBUG: Bar marker - resetting counters");
                }
                break;
                
            case TOKEN_ERROR:
                debug_post(x, "SheetMidi DEBUG: Error token encountered");
                clear_events(x);
                return 0;
        }
    }
    
    // Handle last bar if it wasn't terminated
    if (chords_in_current_bar > 0) {
        if (bar_has_dots) {
            x->events[event_idx - 1].duration = 1 + current_chord_dots;
            debug_post(x, "SheetMidi DEBUG: Final bar (with dots) - last chord duration %d", 
                 x->events[event_idx - 1].duration);
        } else {
            debug_post(x, "SheetMidi DEBUG: Final bar (without dots) - distributing beats among %d chords", 
                 chords_in_current_bar);
            distribute_beats_in_bar(x->events, current_bar_start, 
                                  chords_in_current_bar, x->time_signature);
        }
    }
    
    // Calculate total duration
    x->total_duration = 0;
    for (int i = 0; i < x->num_events; i++) {
        x->total_duration += x->events[i].duration;
    }
    
    debug_post(x, "SheetMidi DEBUG: Parsing complete - %d events, total duration %d beats", 
         x->num_events, x->total_duration);
    
    // Output initial beat position after parsing
    output_beat_position(x);
    
    return 1;
}

// Print the parsed sequence for debugging
static void print_parsed_sequence(t_p_sheetmidi *x) {
    if (!x || !x->events || x->num_events == 0) {
        info_post("SheetMidi: No sequence to print");
        return;
    }
    
    info_post("SheetMidi: Parsed sequence (%d events, total duration: %d beats):", 
         x->num_events, x->total_duration);
    
    int current_bar = 0;
    int beats_in_bar = 0;
    
    for (int i = 0; i < x->num_events; i++) {
        t_chord_event *ev = &x->events[i];
        
        if (beats_in_bar + ev->duration > x->time_signature) {
            current_bar++;
            beats_in_bar = 0;
            info_post("  |");
        }
        
        info_post("    Event %d (bar %d, beat %d): %s (%d beats)", 
             i + 1, current_bar + 1, beats_in_bar + 1,
             ev->chord->s_name, ev->duration);
        
        debug_print_chord("      Chord data", &ev->parsed);
        
        beats_in_bar += ev->duration;
    }
}

// Playback methods
void p_sheetmidi_note(t_p_sheetmidi *x) {
    t_chord_event *ev = get_current_event(x);
    if (!ev) return;
    
    output_debug_chord(x, ev);
    
    if (ev->parsed.num_intervals <= 0) {
        t_float note = ev->parsed.root_offset;
        outlet_float(x->note_outlet, note);
        return;
    }
    
    int random_idx = rand() % ev->parsed.num_intervals;
    t_float note = ev->parsed.root_offset + ev->parsed.intervals[random_idx];
    outlet_float(x->note_outlet, note);
}

void p_sheetmidi_root(t_p_sheetmidi *x) {
    t_chord_event *ev = get_current_event(x);
    if (!ev) return;
    
    output_debug_chord(x, ev);
    t_float note = ev->parsed.root_offset + ev->parsed.intervals[0];
    outlet_float(x->note_outlet, note);
}

void p_sheetmidi_third(t_p_sheetmidi *x) {
    t_chord_event *ev = get_current_event(x);
    if (!ev || ev->parsed.num_intervals < 2) return;
    
    output_debug_chord(x, ev);
    t_float note = ev->parsed.root_offset + ev->parsed.intervals[1];
    outlet_float(x->note_outlet, note);
}

void p_sheetmidi_fifth(t_p_sheetmidi *x) {
    t_chord_event *ev = get_current_event(x);
    if (!ev || ev->parsed.num_intervals < 3) return;
    
    output_debug_chord(x, ev);
    t_float note = ev->parsed.root_offset + ev->parsed.intervals[2];
    outlet_float(x->note_outlet, note);
}

void p_sheetmidi_tick(t_p_sheetmidi *x) {
    if (x->num_events == 0) return;
    
    x->current_beat++;
    if (x->current_beat >= x->total_duration) {
        x->current_beat = 0;
    }
    output_debug_chord(x, get_current_event(x));
    output_beat_position(x);
}

// Add beat handler for left inlet
void p_sheetmidi_beat(t_p_sheetmidi *x, t_float f) {
    reset_beat(x, f);
    output_beat_position(x);
}

void *p_sheetmidi_new(t_symbol *s, int argc, t_atom *argv) {
    t_p_sheetmidi *x = (t_p_sheetmidi *)pd_new(p_sheetmidi_class);
    
    x->p.x = x;
    x->p.pd = p_sheetmidi_proxy_class;
    inlet_new(&x->x_obj, &x->p.pd, 0, 0);
    
    x->time_signature = 4;
    x->events = NULL;
    x->num_events = 0;
    x->total_duration = 0;
    x->debug_enabled = 0;  // Default to debug disabled
    x->current_beat = 0;
    
    // Parse creation arguments
    for (int i = 0; i < argc; i++) {
        if (argv[i].a_type == A_SYMBOL) {
            t_symbol *arg = atom_getsymbol(&argv[i]);
            if (strcmp(arg->s_name, "--debug") == 0) {
                x->debug_enabled = 1;
                info_post("SheetMidi: Debug output enabled");
            }
        }
    }
    
    x->note_outlet = outlet_new(&x->x_obj, &s_float);
    x->beat_outlet = outlet_new(&x->x_obj, &s_float);  // Add new beat position outlet
    x->debug_outlet = outlet_new(&x->x_obj, &s_symbol);
    
    return (void *)x;
}

void p_sheetmidi_free(t_p_sheetmidi *x) {
    clear_events(x);
    if (last_sequence) {
        freebytes(last_sequence, last_sequence_size * sizeof(t_atom));
        last_sequence = NULL;
        last_sequence_size = 0;
    }
}

EXTERN void p_sheetmidi_setup(void) {
    p_sheetmidi_proxy_class = class_new(gensym("p_sheetmidi_proxy"),
        0, 0, sizeof(t_p_sheetmidi_proxy),
        CLASS_PD, 0);
    class_addanything(p_sheetmidi_proxy_class, p_sheetmidi_proxy_anything);
    
    p_sheetmidi_class = class_new(gensym("p_sheetmidi"),
        (t_newmethod)p_sheetmidi_new,
        (t_method)p_sheetmidi_free,
        sizeof(t_p_sheetmidi),
        CLASS_DEFAULT,
        A_GIMME,  // Accept any number of arguments
        0);
    
    class_addbang(p_sheetmidi_class, p_sheetmidi_bang);
    
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
    
    // Add beat method to handle beat messages in left inlet
    class_addmethod(p_sheetmidi_class,
                   (t_method)p_sheetmidi_beat,
                   gensym("beat"),
                   A_FLOAT,
                   0);
    
    info_post("SheetMidi: external loaded");
}



