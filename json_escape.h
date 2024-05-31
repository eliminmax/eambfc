/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only */

#ifndef EAMBFC_JSON_ESCAPE_H
#define EAMBFC_JSON_ESCAPE_H 1
/* return a pointer to a JSON-escaped version of the input string
 * calling function is responsible for freeing it */
char *jsonStr(char* str);
#endif /* EAMBFC_JSON_ESCAPE_H */
