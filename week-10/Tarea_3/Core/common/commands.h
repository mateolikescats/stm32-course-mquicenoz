/*
 * commands.h
 * What each command does.
 * Author: Mateo Quiceno Zapata
 */

#ifndef COMMANDS_H
#define COMMANDS_H

#include <stddef.h>
#include "cmd_parser.h"

/* Runs cmd and writes the answer in reply */
void commands_execute(const cmd_t *cmd, char *reply, size_t reply_size);

#endif /* COMMANDS_H */
