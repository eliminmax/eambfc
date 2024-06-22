/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Handle error messages */

/* C99 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
/* internal */
#include "eambfc_types.h"
#include "json_escape.h"

static bool _quiet;
static bool _json;

/* the only external access to the variables is through these functions. */
void quietMode(void) { _quiet = true; }
void jsonMode(void) { _json = true; }

/* simple enough to inline this one, I think. */
static inline void basicErrJSON(char* id, char *msg) {
    /* assume error id is json-safe, but don't assume that for msg. */
    if ((msg = jsonStr(msg)) != NULL) {
        printf("{\"errorId\":\"%s\",\"message\":\"%s\"}\n", id, msg);
        free(msg);
    }
}

void basicError(char* id, char *msg) {
    if (_json) basicErrJSON(id, msg);
    else if (_quiet) return;
    else fprintf(stderr, "Error %s: %s\n", id, msg);
}

/* with up to 2 frees needed, and 2 early exits if they fail, don't inline */
static void posErrJSON(char *id, char *msg, char instr, uint line, uint col) {
    /* Assume id needs no escaping, but msg and instr might. */
    /* First, convert instr into a string, then serialize that string. */
    char instr_str[2] = { instr, '\0' };
    char *instr_json;
    if ((instr_json = jsonStr(instr_str)) == NULL) return;
    if ((msg = jsonStr(msg)) == NULL) {
        free(instr_json);
        return;
    }
    printf(
        "{\"errorId\":\"%s\",\"message\":\"%s\",\"instruction\":\"%s\","
        "\"line\":%u,\"column\":%u}\n", id, msg, instr_json, line, col
    );
    free(msg);
    free(instr_json);
}

void positionError(char *id, char *msg, char instr, uint line, uint col) {
    if (_json) posErrJSON(id, msg, line, col, instr);
    else if (_quiet) return;
    else {
        fprintf(
            stderr,
            "Error %s when compiling '%c' at line %u, column %u: %s\n",
            id, instr, line, col, msg
        );
    }
}

static void instrErrJSON(char *id, char *msg, char instr) {
    /* Assume id needs no escaping, but msg and instr might. */
    /* First, convert instr into a string, then serialize that string. */
    char instr_str[2] = { instr, '\0' };
    char *instr_json;
    if ((instr_json = jsonStr(instr_str)) == NULL) return;
    if ((msg = jsonStr(msg)) == NULL) {
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

void instructionError(char *id, char *msg, char instr) {
    if (_json) instrErrJSON(id, msg, instr);
    else if (_quiet) return;
    else fprintf(stderr, "Error %s when compiling '%c: %s'.\n", id, instr, msg);
}
