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

#define bs_escaped(c) *(outp++) = '\\'; *(outp++) = c; used += 2; break

/* return a pointer to a JSON-escaped version of the input string
 * calling function is responsible for freeing it */
char *jsonStr(char* str) {
    size_t bufsz = strlen(str) + 16; /* +16 for padding */
    size_t used = 0;
    char *p = str;
    char *json_escaped = malloc(bufsz);
    char *reallocator;
    char *outp = json_escaped;
    if (json_escaped == NULL) return NULL;
    p = str;
    while (*p) {
        switch(*p) {
          case '\n': bs_escaped('n');
          case '\r': bs_escaped('r');
          case '\f': bs_escaped('f');
          case '\t': bs_escaped('t');
          case '\b': bs_escaped('b');
          case '\\': bs_escaped('\\');
          case '\"': bs_escaped('\"');
          default:
            /* would use a switch statement, but case a ... d is non-portable.
             * Instead, I went with this ugly hybrid system */
            if ((unsigned char)(*p) < 040) { /* control chars are 000 to 037 */
                sprintf(outp, "\\u%04hhx", *p);
                used += 6;
                outp += 6;
            } else {
                used++;
                *(outp++) = *p;
            }
            break;
        }
        /* If less than 8 chars are available, allocate more space */
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
