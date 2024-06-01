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

#define bs_escaped(c) *(outp++) = '\\'; *(outp++) = c; used += 2

/* return a pointer to a JSON-escaped version of the input string
 * calling function is responsible for freeing it */
char *jsonStr(char* str) {
    size_t bufsz = strlen(str) + 16; /* +16 for padding */
    size_t used = 0;
    char *p = str;
    char *json_escaped = (char *)malloc(bufsz);
    char *outp = json_escaped;
    if (json_escaped == NULL) return "malloc failed for json_escape";
    p = str;
    while (*p) {
        switch(*p) {
          case '\n':
            bs_escaped('n');
            break;
          case '\r':
            bs_escaped('r');
            break;
          case '\f':
            bs_escaped('f');
            break;
          case '\t':
            bs_escaped('t');
            break;
          case '\b':
            bs_escaped('b');
            break;
          case '\\':
            bs_escaped('\\');
            break;
          case '\"':
            bs_escaped('\"');
            break;
          default:
            if (*p < 040) {
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
            json_escaped = (char *)realloc(json_escaped, bufsz);
            if (json_escaped == NULL) return "malloc failed for json_escape";
        }
        p++;
    }
    *outp = 0;
    return json_escaped;
}
