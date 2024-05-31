/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only */

#ifndef JSON_ESCAPE_H
#define JSON_ESCAPE_H 1
/* return a pointer to a JSON-escaped version of the input string
 * calling function is responsible for freeing it */
char *json_escape(char* str);
#endif
