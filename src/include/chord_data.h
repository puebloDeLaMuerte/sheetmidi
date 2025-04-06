#ifndef CHORD_DATA_H
#define CHORD_DATA_H

#include "m_pd.h"

typedef struct _chord_data {
    t_symbol *original;     // Original chord symbol
    int root_offset;        // Semitones from C (0-11)
    int intervals[12];      // Array of intervals in semitones
    int num_intervals;      // Number of intervals used
} t_chord_data;

typedef struct _chord_event {
    t_symbol *chord;     // The chord symbol (like "C", "Dm7", etc.)
    int duration;        // Duration in beats
    t_chord_data parsed; // Parsed chord data
} t_chord_event;

// Function declarations
t_chord_data parse_chord_symbol(t_symbol *sym);
void debug_print_chord(const char* prefix, const t_chord_data* chord);

#endif // CHORD_DATA_H 