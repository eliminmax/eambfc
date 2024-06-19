/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file defines a function that can be used to JSON-escape a string.
 *
 * It doesn't fit anywhere else in the code, so it's on its own in here. */

/* C99 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
/* POSIX */
#include <unistd.h>

/* macro specifically for use inside jsonStr, to avoid ugly, repeated code. */
#define BS_ESCAPED(c) *(outp++) = '\\'; *(outp++) = c; used += 2

/* return a pointer to a JSON-escaped version of the input string
 * calling function is responsible for freeing it */
char *jsonStr(char* str) {
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
          case '\n': BS_ESCAPED('n'); break;
          case '\r': BS_ESCAPED('r'); break;
          case '\f': BS_ESCAPED('f'); break;
          case '\t': BS_ESCAPED('t'); break;
          case '\b': BS_ESCAPED('b'); break;
          case '\\': BS_ESCAPED('\\'); break;
          case '\"': BS_ESCAPED('\"'); break;
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
    #undef BS_ESCAPED
}

/* DANGEROUS FUNCTION ON UNTRUSTED `fmt`! Used with care in this codebase. */
void printJsonError(char *fmt, char *str) {
    char *json_str = jsonStr(str);
    if (json_str == NULL) {
        puts(
            "{\"errorId\":\"ICE_ICE_BABY\","
            "\"message:\"Failed to allocate memory to generate "
            "JSON-escaped string\"}"
        );
    } else {
        printf(fmt, json_str);
        free(json_str);
    }
}
