/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Handle error messages */

/* C99 */
#include <stdio.h> /* puts, fputs, *printf */
#include <stdlib.h> /* malloc, realloc, free */
#include <string.h> /* strlen, strcpy, strstr, memmove, memcpy */
/* internal */
#include "types.h" /* bool, uint */

static bool _quiet;
static bool _json;

/* the only external access to the variables is through these functions. */
void quiet_mode(void) { _quiet = true; }
void json_mode(void) { _json = true; }

/* avoid using json_str for this special case, as malloc may fail again,
 * causing a loop of failures to generate json error messages properly. */
void alloc_err(void) {
    if (_json) {
        puts(
            "{\"errorId:\":\"ALLOC_FAILED\","
            "\"message\":\"A call to malloc or realloc returned NULL.\"}"
        );
    } else if (!_quiet) {
        fputs(
            "Error ALLOC_FAILED: A call to malloc or realloc returned NULL.\n",
            stderr
        );
    }
}

/* macro specifically for use inside json_str, to avoid ugly, repeated code. */
#define BS_ESCAPE_APPEND(c) *(outp++) = '\\'; *(outp++) = c; used += 2
/* return a pointer to a JSON-escaped version of the input string
 * calling function is responsible for freeing it */
static char *json_str(char* str) {
    size_t bufsz = strlen(str) + 16; /* 16 for padding, more added as needed */
    size_t used = 0;
    char *p = str;
    char *json_escaped = malloc(bufsz);
    char *reallocator;
    char *outp = json_escaped;
    if (json_escaped == NULL) return NULL;
    p = str;
    while (*p) {
        switch(*p) {
          case '\n': BS_ESCAPE_APPEND('n'); break;
          case '\r': BS_ESCAPE_APPEND('r'); break;
          case '\f': BS_ESCAPE_APPEND('f'); break;
          case '\t': BS_ESCAPE_APPEND('t'); break;
          case '\b': BS_ESCAPE_APPEND('b'); break;
          case '\\': BS_ESCAPE_APPEND('\\'); break;
          case '\"': BS_ESCAPE_APPEND('\"'); break;
          default:
            /* would prefer a pure switch statement, but `case a ... d` is
             * a non-standard (though common) extension to C, and is not
             * Using this `if`-within-a-`switch` instead. */
            if ((unsigned char)(*p) < 040) { /* control chars are 000 to 037 */
                sprintf(outp, "\\u%04hhx", *p);
                used += 6;
                outp += 6;
            } else {
                /* the true default case. Character needs no escaping. */
                used++;
                *(outp++) = *p;
            }
            break;
        }
        /* If less than 8 chars are left before overflow, allocate more space */
        if (used > (bufsz - 8)) {
            bufsz += 16;
            if (((reallocator = realloc(json_escaped, bufsz)) == NULL)) {
                free(json_escaped);
                return NULL;
            }
            json_escaped = reallocator;
        }
        p++;
    }
    *outp = 0;
    return json_escaped;
}
#undef BS_ESCAPE_APPEND


/* simple enough to inline this one, I think. */
static inline void basic_jerr(char* id, char *msg) {
    /* assume error id is json-safe, but don't assume that for msg. */
    if ((msg = json_str(msg)) == NULL) {
        alloc_err();
    } else {
        printf("{\"errorId\":\"%s\",\"message\":\"%s\"}\n", id, msg);
        free(msg);
    }
}

void basic_err(char* id, char *msg) {
    if (_json) basic_jerr(id, msg);
    else if (!_quiet) fprintf(stderr, "Error %s: %s\n", id, msg);
}

/* with up to 2 frees needed, and 2 early exits if they fail, don't inline */
static void pos_jerr(char *id, char *msg, char instr, uint line, uint col) {
    /* Assume id needs no escaping, but msg and instr might. */
    /* First, convert instr into a string, then serialize that string. */
    char instr_str[2] = { instr, '\0' };
    char *instr_json;
    if ((instr_json = json_str(instr_str)) == NULL) {
        alloc_err();
        return;
    }
    if ((msg = json_str(msg)) == NULL) {
        free(instr_json);
        alloc_err();
        return;
    }
    printf(
        "{\"errorId\":\"%s\",\"message\":\"%s\",\"instruction\":\"%s\","
        "\"line\":%u,\"column\":%u}\n", id, msg, instr_json, line, col
    );
    free(msg);
    free(instr_json);
}

void position_err(char *id, char *msg, char instr, uint line, uint col) {
    if (_json) pos_jerr(id, msg, instr, line, col);
    else if (!_quiet) {
        fprintf(
            stderr,
            "Error %s when compiling '%c' at line %u, column %u: %s\n",
            id, instr, line, col, msg
        );
    }
}

static void instr_jerr(char *id, char *msg, char instr) {
    /* Assume id needs no escaping, but msg and instr might. */
    /* First, convert instr into a string, then serialize that string. */
    char instr_str[2] = { instr, '\0' };
    char *instr_json;
    if ((instr_json = json_str(instr_str)) == NULL) {
        alloc_err();
        return;
    }
    if ((msg = json_str(msg)) == NULL) {
        alloc_err();
        free(instr_json);
        return;
    }
    printf(
        "{\"errorId\":\"%s\",\"message\":\"%s\",\"instruction\":\"%s\"}\n",
        id, msg, instr_json
    );
    free(msg);
    free(instr_json);
}

void instr_err(char *id, char *msg, char instr) {
    if (_json) instr_jerr(id, msg, instr);
    else if (!_quiet) {
        fprintf(stderr, "Error %s when compiling '%c: %s'.\n", id, instr, msg);
    }
}

void param_err(char *id, char *proto, char *arg) {
    char *inj_point;
    size_t proto_sz = strlen(proto);
    size_t arg_sz = strlen(arg);
    /* add 1 for the null byte, but sub 2 as the first "{}" is removed.
     * simplifies to -1 total, but must be at least as large as proto_sz, so
     * don't subtract it. */
    char *msg = malloc(proto_sz + arg_sz);
    if (msg == NULL) {
        alloc_err();
        return;
    }
    strcpy(msg, proto);
    inj_point = strstr(msg, "{}");
    if (inj_point == NULL) {
        param_err(
            "PARAMETER_ERROR_ERROR",
            "Prototype \"{}\" does not contain substring \"{}\".",
            proto
        );
        free(msg);
        return;
    }
    memmove(inj_point + arg_sz, inj_point + 2, strlen(inj_point + 2) + 1);
    memcpy(inj_point, arg, arg_sz);
    basic_err(id, msg);
    free(msg);
}
