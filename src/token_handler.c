#include "include/token_handler.h"
#include <string.h>
#include <ctype.h>

token_t atom_to_token(t_atom *atom) {
    token_t token = {TOKEN_ERROR, NULL};
    
    if (atom->a_type != A_SYMBOL) {
        post("SheetMidi: Got non-symbol atom");
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

int tokenize_string(t_p_sheetmidi *x, const char *str, token_t **tokens, int *num_tokens) {
    if (!x || !str || !tokens || !num_tokens) return 0;
    
    // Skip UTF-8 BOM if present
    if ((unsigned char)str[0] == 0xEF && 
        (unsigned char)str[1] == 0xBB && 
        (unsigned char)str[2] == 0xBF) {
        str += 3;
    }
    
    // Skip leading whitespace and non-printable characters
    while (*str && (!isprint((unsigned char)*str) || isspace((unsigned char)*str))) str++;
    
    // First pass: count tokens
    int max_tokens = 0;
    const char *p = str;
    int in_token = 0;
    
    while (*p) {
        if (!isprint((unsigned char)*p)) {
            p++;
            continue;
        }
        if (*p == '|') {
            if (in_token) max_tokens++;
            max_tokens++; // Count the bar itself
            in_token = 0;
        } else if (isspace((unsigned char)*p)) {
            if (in_token) max_tokens++;
            in_token = 0;
        } else if (!in_token) {
            in_token = 1;
        }
        p++;
    }
    if (in_token) max_tokens++;
    
    // Allocate token array
    *tokens = (token_t *)getbytes(max_tokens * sizeof(token_t));
    if (!*tokens) return 0;
    *num_tokens = 0;
    
    // Second pass: create tokens
    p = str;
    char token_buf[256] = {0};
    int token_len = 0;
    
    while (*p) {
        if (!isprint((unsigned char)*p)) {
            p++;
            continue;
        }
        if (*p == '|') {
            // Output any pending token
            if (token_len > 0) {
                token_buf[token_len] = '\0';
                // Trim trailing whitespace
                while (token_len > 0 && isspace((unsigned char)token_buf[token_len - 1])) {
                    token_buf[--token_len] = '\0';
                }
                if (token_len > 0) {
                    (*tokens)[*num_tokens].type = TOKEN_CHORD;
                    (*tokens)[*num_tokens].value = gensym(token_buf);
                    (*num_tokens)++;
                }
                token_len = 0;
            }
            
            // Output the bar marker
            (*tokens)[*num_tokens].type = TOKEN_BAR;
            (*tokens)[*num_tokens].value = NULL;
            (*num_tokens)++;
        } else if (isspace((unsigned char)*p)) {
            if (token_len > 0) {
                token_buf[token_len] = '\0';
                // Trim trailing whitespace
                while (token_len > 0 && isspace((unsigned char)token_buf[token_len - 1])) {
                    token_buf[--token_len] = '\0';
                }
                if (token_len > 0) {
                    (*tokens)[*num_tokens].type = TOKEN_CHORD;
                    (*tokens)[*num_tokens].value = gensym(token_buf);
                    (*num_tokens)++;
                }
                token_len = 0;
            }
        } else {
            if (token_len < sizeof(token_buf) - 1) {
                token_buf[token_len++] = *p;
            }
        }
        p++;
    }
    
    // Handle any final token
    if (token_len > 0) {
        token_buf[token_len] = '\0';
        // Trim trailing whitespace
        while (token_len > 0 && isspace((unsigned char)token_buf[token_len - 1])) {
            token_buf[--token_len] = '\0';
        }
        if (token_len > 0) {
            (*tokens)[*num_tokens].type = TOKEN_CHORD;
            (*tokens)[*num_tokens].value = gensym(token_buf);
            (*num_tokens)++;
        }
    }
    
    return 1;
} 