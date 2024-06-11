/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file declares the interface to the jsonStr function in json_escape.c */

#ifndef EAMBFC_JSON_ESCAPE_H
#define EAMBFC_JSON_ESCAPE_H 1
/* return a pointer to a JSON-escaped version of the input string
 * calling function is responsible for freeing it */
char *jsonStr(char* str);
void printJsonError(char *fmt, char *s);
#endif /* EAMBFC_JSON_ESCAPE_H */
