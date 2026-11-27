/**
 ******************************************************************************
 * @file           : cmd_parser.c
 * @author         : Mateo Quiceno Zapata
 * @brief          : Splits a text line into a command and its arguments.
 ******************************************************************************
 *** DESCRIPTION

 No hardware at all: it compiles on a PC too. The line is split in place:
 every separator after a word becomes '\0', and cmd_t keeps pointers into
 the same line.

   line:  "blink   500"
   after: "blink\0  500\0"  -> name = "blink", argc = 1, argv[0] = "500"

 The first word is looked up in cmd_table and mapped to a cmd_id_t, so
 commands.c can use a switch (direct calls, no function pointers: Power of
 10, rule 9).

 ***ADDING A COMMAND (the four places)

 +----------------+---------------------------+--------------------------+
 | File           | What you add              | What it does             |
 +----------------+---------------------------+--------------------------+
 | cmd_parser.h   | CMD_xxx in the enum       | identity of the command  |
 | cmd_parser.c   | { "xxx", CMD_xxx } below  | typed word -> identity   |
 | commands.c     | case CMD_xxx + cmd_xxx()  | what the command does    |
 | commands.c     | "xxx" in the help text    | the user can discover it |
 +----------------+---------------------------+--------------------------+
 */

#include <stddef.h>
#include <string.h>
#include "cmd_parser.h"

//one row of the table: the word the user types and its enum value
typedef struct
{
	const char *name;
	cmd_id_t id;
} cmd_entry_t;

//const: lives in flash. Adding a command = one row here + one case in commands.c
static const cmd_entry_t cmd_table[] =
{
	{ "help",  CMD_HELP  },
	{ "info",  CMD_INFO  },
	{ "blink", CMD_BLINK },
	{ "echo",  CMD_ECHO  },
	{ "pwm",   CMD_PWM   }, //Stage 5
};

//number of rows, computed by the compiler (total bytes / bytes per row)
#define CMD_TABLE_LEN (sizeof cmd_table / sizeof cmd_table[0])

//space and tab separate words
static bool is_separator(char c)
{
	return (c == ' ') || (c == '\t');
}

//moves p past any spaces; returns the start of the next word (or the '\0')
static char *skip_separators(char *p)
{
	while ((*p != '\0') && is_separator(*p))
	{
		p++;
	}
	return p;
}

//moves p to the end of the current word and writes '\0' there, so the word
//becomes its own string. Returns the position just after it.
static char *end_word(char *p)
{
	while ((*p != '\0') && !is_separator(*p))
	{
		p++;
	}
	if (*p != '\0') //stopped on a separator, not on the end of the line
	{
		*p = '\0'; //cut the word here
		p++;
	}
	return p;
}

//finds the command name in the table; CMD_UNKNOWN if it is not there
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

//"blink   500" -> name "blink", argc 1, argv[0] "500". Pointers point inside 'line'
bool cmd_parse(char *line, cmd_t *cmd)
{
	char *p = skip_separators(line); //ignore leading spaces

	//safe defaults, so the caller never reads garbage
	cmd->id = CMD_UNKNOWN;
	cmd->name = p;
	cmd->argc = 0U;
	cmd->too_many_args = false;

	if (*p == '\0') //only spaces or nothing: empty line
	{
		return false;
	}

	p = end_word(p); //first word = command name
	cmd->id = lookup(cmd->name);

	p = skip_separators(p);
	while (*p != '\0') //each remaining word is an argument
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
