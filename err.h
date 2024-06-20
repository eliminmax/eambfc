/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Functions and data storage to handle error messages */
#ifndef EAM_ERR_H
#define EAM_ERR_H 1
/* internal */
#include "eambfc_types.h"

/* enable quiet mode - this does not print error messages to stderr. */
void quietMode(void);
/* enable json display mode - this prints JSON-formatted error messagess to
 * stdout instead of printing human-readable error messages to stderr. */
void jsonMode(void);

/* functions to display error messages, depending on the current mode. */
/* a generic error message */
void basicError(char* id, char *msg);
/* an error message related to a specific instruction */
void instructionError(char *id, char *msg, char instr);
/* an error message related to a specific instruction at a specific location */
void positionError(char *id, char *msg, char instr, uint line, uint col);

#endif /* EAM_ERR_H */
