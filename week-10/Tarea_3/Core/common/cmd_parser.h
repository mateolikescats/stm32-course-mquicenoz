/*
 * cmd_parser.h
 * Splits a line into a command and its arguments.
 * Author: Mateo Quiceno Zapata
 */

#ifndef CMD_PARSER_H
#define CMD_PARSER_H

#include <stdbool.h>
#include <stdint.h>

#define CMD_MAX_ARGS 4U

typedef enum
{
	CMD_HELP,
	CMD_INFO,
	CMD_BLINK,
	CMD_ECHO,
	CMD_PWM,     /* Stage 5 */
	CMD_UNKNOWN
} cmd_id_t;

typedef struct
{
	cmd_id_t id;                     /* command id */
	const char *name;                /* first word */
	uint8_t argc;                    /* number of arguments */
	const char *argv[CMD_MAX_ARGS];  /* arguments */
	bool too_many_args;
} cmd_t;

/* Splits line in place, false = empty line */
bool cmd_parse(char *line, cmd_t *cmd);

#endif /* CMD_PARSER_H */
