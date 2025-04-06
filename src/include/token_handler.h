#ifndef TOKEN_HANDLER_H
#define TOKEN_HANDLER_H

#include "m_pd.h"
#include "p_sheetmidi_types.h"

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

// Function declarations
token_t atom_to_token(t_atom *atom);
int tokenize_string(t_p_sheetmidi *x, const char *str, token_t **tokens, int *num_tokens);

#endif // TOKEN_HANDLER_H 