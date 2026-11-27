/*
 * cmd_parser.c
 * Splits a line into a command and its arguments.
 * "blink 500" -> name "blink", argv[0] "500"
 * Author: Mateo Quiceno Zapata
 */

#include <stddef.h>
#include <string.h>
#include "cmd_parser.h"

//one row: word and its id
typedef struct
{
	const char *name;
	cmd_id_t id;
} cmd_entry_t;

//command table (flash)
static const cmd_entry_t cmd_table[] =
{
	{ "help",  CMD_HELP  },
	{ "info",  CMD_INFO  },
	{ "blink", CMD_BLINK },
	{ "echo",  CMD_ECHO  },
	{ "pwm",   CMD_PWM   }, //Stage 5
};

//number of rows
#define CMD_TABLE_LEN (sizeof cmd_table / sizeof cmd_table[0])

//space and tab separate words
static bool is_separator(char c)
{
	return (c == ' ') || (c == '\t');
}

//skips spaces
static char *skip_separators(char *p)
{
	while ((*p != '\0') && is_separator(*p))
	{
		p++;
	}
	return p;
}

//ends the word with '\0'
static char *end_word(char *p)
{
	while ((*p != '\0') && !is_separator(*p))
	{
		p++;
	}
	if (*p != '\0') //separator found
	{
		*p = '\0'; //cut the word here
		p++;
	}
	return p;
}

//name -> id
static cmd_id_t lookup(const char *name)
{
	for (size_t i = 0U; i < CMD_TABLE_LEN; i++)
	{
		if (strcmp(name, cmd_table[i].name) == 0) //0 = equal strings
		{
			return cmd_table[i].id;
		}
	}
	return CMD_UNKNOWN;
}

//line -> cmd
bool cmd_parse(char *line, cmd_t *cmd)
{
	char *p = skip_separators(line); //ignore leading spaces

	//defaults
	cmd->id = CMD_UNKNOWN;
	cmd->name = p;
	cmd->argc = 0U;
	cmd->too_many_args = false;

	if (*p == '\0') //empty line
	{
		return false;
	}

	p = end_word(p); //first word = command name
	cmd->id = lookup(cmd->name);

	p = skip_separators(p);
	while (*p != '\0') //the rest are arguments
	{
		if (cmd->argc >= CMD_MAX_ARGS) //argv[] is full
		{
			cmd->too_many_args = true;
			break;
		}
		cmd->argv[cmd->argc] = p; //argument starts here
		cmd->argc++;
		p = end_word(p);
		p = skip_separators(p);
	}

	return true;
}
