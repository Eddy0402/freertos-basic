#ifndef SHELL_H
#define SHELL_H

void command_prompt(void *pvParameters);
int parse_command(char *str, char *argv[]);

typedef void cmdfunc(int, char *[]);

cmdfunc *find_command(const char *str);

#endif
