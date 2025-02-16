#include "include/m_pd.h"

EXTERN void pd_init(t_pd *x);  // Add this declaration

static t_class *p_sheetmidi_class;
static t_class *p_sheetmidi_proxy_class;  // Class for right inlet proxy

typedef struct _p_sheetmidi_proxy {
    t_pd pd;
    struct _p_sheetmidi *x;
} t_p_sheetmidi_proxy;

typedef struct _p_sheetmidi {
    t_object x_obj;
    t_symbol **chords;      // Array of chord symbols
    int num_chords;         // Number of chords stored
    t_p_sheetmidi_proxy p;  // Proxy for right inlet
} t_p_sheetmidi;

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
    if (x->num_chords == 0) {
        post("SheetMidi: No chords stored");
        return;
    }
    
    post("SheetMidi: Current progression (%d chords):", x->num_chords);
    for (int i = 0; i < x->num_chords; i++) {
        post("  Chord %d: %s", i + 1, x->chords[i]->s_name);
    }
}

// Right inlet handlers (via proxy)
void p_sheetmidi_proxy_symbol(t_p_sheetmidi_proxy *p, t_symbol *s) {
    p_sheetmidi_store_chords(p->x, 1, &s);
}

void p_sheetmidi_proxy_list(t_p_sheetmidi_proxy *p, t_symbol *s, int argc, t_atom *argv) {
    t_symbol **symbols = (t_symbol **)getbytes(argc * sizeof(t_symbol *));
    int valid_count = 0;
    
    for (int i = 0; i < argc; i++) {
        if (argv[i].a_type == A_SYMBOL) {
            symbols[valid_count++] = atom_getsymbol(&argv[i]);
        }
    }
    
    if (valid_count > 0) {
        p_sheetmidi_store_chords(p->x, valid_count, symbols);
    }
    
    freebytes(symbols, argc * sizeof(t_symbol *));
}

// Main object list handler - explicitly ignore lists
void p_sheetmidi_list(t_p_sheetmidi *x, t_symbol *s, int argc, t_atom *argv) {
    // Do nothing - we don't want to handle lists on the main object
}

void *p_sheetmidi_new(void) {
    t_p_sheetmidi *x = (t_p_sheetmidi *)pd_new(p_sheetmidi_class);
    
    // Initialize proxy for right inlet
    x->p.x = x;
    x->p.pd = p_sheetmidi_proxy_class;  // Set the class pointer
    inlet_new(&x->x_obj, &x->p.pd, 0, 0);
    
    // Initialize chord storage
    x->chords = NULL;
    x->num_chords = 0;
    
    post("SheetMidi: new instance created");
    return (void *)x;
}

void p_sheetmidi_free(t_p_sheetmidi *x) {
    if (x->chords) {
        freebytes(x->chords, x->num_chords * sizeof(t_symbol *));
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


