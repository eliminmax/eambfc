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

/* return a pointer to a JSON-escaped version of the input string
 * calling function is responsible for freeing it */
char *jsonStr(char* str) {
    size_t bufsz = 1; /* start with 1 for the null terminator at the end */
    char *p = str;
    /* 2 passes - 1 to determine bufsz, and 1 to actually build the string. */
    while (*p) {
        switch(*p) {
          case '\n':
          case '\r':
          case '\f':
          case '\t':
          case '\b':
          case '\\':
          case '\"':
            bufsz += 2;
            break;
          default:
        /* was going to be a switch statement, but case a ... d is non-portable.
         * Instead, I went with this ugly hybrid system */
            if (*p < 040) {
                bufsz += 6;
            } else {
                bufsz++;
            }
            break;
        }
        p++;
    }
    char *json_escaped = (char *)malloc(bufsz);
    char *outp = json_escaped;
    if (json_escaped == NULL) return "malloc failed for json_escape";
    p = str;
    while (*p) {
        switch(*p) {
          case '\n':
            *(outp++) = '\\';
            *(outp++) = 'n';
            break;
          case '\r':
            *(outp++) = '\\';
            *(outp++) = 'r';
            break;
          case '\f':
            *(outp++) = '\\';
            *(outp++) = 'f';
            break;
          case '\t':
            *(outp++) = '\\';
            *(outp++) = 't';
            break;
          case '\b':
            *(outp++) = '\\';
            *(outp++) = 'b';
            break;
          case '\\':
            *(outp++) = '\\';
            *(outp++) = '\\';
            break;
          case '\"':
            *(outp++) = '\\';
            *(outp++) = '\"';
            break;
          default:
            if (*p < 040) {
                sprintf(outp, "\\u%04hhx", *p);
                outp += 6;
            } else {
                *(outp++) = *p;
            }
            break;
        }
        p++;
    }
    *outp = 0;
    return json_escaped;
}
