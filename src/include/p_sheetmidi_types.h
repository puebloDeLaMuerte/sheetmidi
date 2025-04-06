#ifndef P_SHEETMIDI_TYPES_H
#define P_SHEETMIDI_TYPES_H

#include "m_pd.h"
#include "chord_data.h"

// Forward declarations
struct _p_sheetmidi;

typedef struct _p_sheetmidi_proxy {
    t_pd pd;
    struct _p_sheetmidi *x;
} t_p_sheetmidi_proxy;

typedef struct _p_sheetmidi {
    t_object x_obj;
    t_p_sheetmidi_proxy p;  // Proxy for right inlet
    
    t_float time_signature; // For validation during parsing only
    t_chord_event *events;  // Array of chord events
    int num_events;         // Number of events
    int total_duration;     // Total duration in beats
    int debug_enabled;      // Flag to control debug output
    
    // Playback members
    t_outlet *note_outlet;     // Outlet for current note value
    t_outlet *debug_outlet;    // Outlet for chord symbols
    int current_beat;          // Current playback position in beats
} t_p_sheetmidi;

#endif // P_SHEETMIDI_TYPES_H 