/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file declares the interface to the jsonStr function in json_escape.c */

#ifndef EAMBFC_JSON_ESCAPE_H
#define EAMBFC_JSON_ESCAPE_H 1


/* return a pointer to a JSON-escaped version of the input string.
 * calling function is responsible for freeing it.
 *
 * If `malloc` or `realloc` fail, returns NULL.
 * It assumes str is UTF-8 encoded, because I'd rather have that documented
 * assumption than either worry about different text encodings or pull in any
 * external dependencies I don't need. */
char *jsonStr(char* str);

/* a wrapper around printf(fmt, jsonStr(s)) that frees the jsonStr once used,
 * and prints a fallbakc error message if jsonStr returns NULL, provided for
 * convenience.
 *
 * Does not validate that `%s` is present in `fmt`, nor that it's the only
 * conversion specifier in `fmt`, so the danger of an untrusted `fmt` is carried
 * over from `printf` itself.
 *
 * DO NOT USE WITH UNTRUSTED `fmt` values! */
void printJsonError(char *fmt, char *s);
#endif /* EAMBFC_JSON_ESCAPE_H */
