#include "shell.h"
#include <stddef.h>
#include "clib.h"
#include <string.h>
#include "fio.h"
#include "filesystem.h"

#include "FreeRTOS.h"
#include "task.h"
#include "host.h"
#include "linenoise.h"

/* The default name, the actual name is define in makefile use -DUSER_NAME*/
#ifndef USER_NAME
#define USER_NAME user
#endif


typedef struct {
    const char *name;
    cmdfunc *fptr;
    const char *desc;
} command;

void ls_command(int, char **);
void man_command(int, char **);
void cat_command(int, char **);
void ps_command(int, char **);
void host_command(int, char **);
void help_command(int, char **);
void host_command(int, char **);
void mmtest_command(int, char **);
void test_command(int, char **);
void _command(int, char **);

int parse_command_args(char *str, char *argv[]);
int find_command_id(const char *cmd);

#define MKCL(n, d) {.name=#n, .fptr=n ## _command, .desc=d}

command cmd_list[]={
    MKCL(ls, "List directory"),
    MKCL(man, "Show the manual of the command"),
    MKCL(cat, "Concatenate files and print on the stdout"),
    MKCL(ps, "Report a snapshot of the current processes"),
    MKCL(host, "Run command on host"),
    MKCL(mmtest, "heap memory allocation test"),
    MKCL(help, "help"),
    MKCL(test, "test new function"),
    MKCL(, ""),
};

int parse_command_args(char *str, char *argv[]){
    int b_quote=0, b_dbquote=0;
    int i;
    int count=0, p=0;
    for(i=0; str[i]; ++i){
        if(str[i]=='\'')
            ++b_quote;
        if(str[i]=='"')
            ++b_dbquote;
        if(str[i]==' '&&b_quote%2==0&&b_dbquote%2==0){
            str[i]='\0';
            argv[count++]=&str[p];
            p=i+1;
        }
    }
    /* last one */
    argv[count++]=&str[p];

    return count;
}

void ls_command(int n, char *argv[]){
    int dir;
    if(n == 0){
        dir = fs_opendir("");
    }else if(n == 1){
        dir = fs_opendir(argv[1]);
        //if(dir == )
    }else{
        fio_printf(1, "Too many argument!\r\n");
        return;
    }
    (void)dir;   // Use dir
}

int filedump(const char *filename){
    char buf[128];

    int fd=fs_open(filename, 0, O_RDONLY);

    if( fd == -2 || fd == -1)
        return fd;

    int count;
    while((count=fio_read(fd, buf, sizeof(buf)))>0){
        fio_write(1, buf, count);
    }

    fio_printf(1, "\r");

    fio_close(fd);
    return 1;
}

void ps_command(int n, char *argv[]){
    signed char buf[1024];
    vTaskList(buf);
    fio_printf(1, "Name          State   Priority  Stack  Num\n\r");
    fio_printf(1, "*******************************************\n\r");
    fio_printf(1, "%s\r\n", buf + 2);	
}

void cat_command(int n, char *argv[]){
    if(n==1){
        fio_printf(2, "Usage: cat <filename>\r\n");
        return;
    }

    int dump_status = filedump(argv[1]);
    if(dump_status == -1){
        fio_printf(2, "%s : no such file or directory.\r\n", argv[1]);
    }else if(dump_status == -2){
        fio_printf(2, "File system not registered.\r\n", argv[1]);
    }
}

void man_command(int n, char *argv[]){
    if(n==1){
        fio_printf(2, "Usage: man <command>\r\n");
        return;
    }

    char buf[128]="/romfs/manual/";
    strcat(buf, argv[1]);

    int dump_status = filedump(buf);
    if(dump_status < 0)
        fio_printf(2, "\r\nManual not available.\r\n");
}

void host_command(int n, char *argv[]){
    int i, len = 0, rnt;
    char command[128] = {0};

    if(n > 1){
        for(i = 1; i < n; i++) {
            memcpy(&command[len], argv[i], strlen(argv[i]));
            len += (strlen(argv[i]) + 1);
            command[len - 1] = ' ';
        }
        command[len - 1] = '\0';
        rnt=host_action(SYS_SYSTEM, command);
        fio_printf(1, "\r\nfinish with exit code %d.\r\n", rnt);
    } else {
        fio_printf(2, "\r\nUsage: host 'command'\r\n");
    }
}

void help_command(int n,char *argv[]){
    const int cmd_count = sizeof(cmd_list) / sizeof(cmd_list[0]);
    for(int i = cmd_count - 2 /* ignore dummy command */; i >= 0; --i){
        fio_printf(1, "%s - %s\r\n", cmd_list[i].name, cmd_list[i].desc);
    }
}

void test_command(int n, char *argv[]) {
    int handle;
    int error;

    handle = host_action(SYS_SYSTEM, "mkdir -p output");
    handle = host_action(SYS_SYSTEM, "touch output/syslog");

    handle = host_action(SYS_OPEN, "output/syslog", 8);
    if(handle == -1) {
        fio_printf(1, "Open file error!\n\r");
        return;
    }

    char *buffer = "Test host_write function which can write data to output/syslog\n";
    error = host_action(SYS_WRITE, handle, (void *)buffer, strlen(buffer));
    if(error != 0) {
        fio_printf(1, "Write file error! Remain %d bytes didn't write in the file.\n\r", error);
        host_action(SYS_CLOSE, handle);
        return;
    }

    host_action(SYS_CLOSE, handle);
}

void _command(int n, char *argv[]){
    (void)n; (void)argv;
}

int find_command_id(const char *cmd){
    const int cmd_count = sizeof(cmd_list) / sizeof(cmd_list[0]);
    for(int i = cmd_count - 1; i >= 0; --i){
        if(strcmp(cmd_list[i].name, cmd) == 0)
            return i;
    }
    return -1;
}

void command_prompt(void *pvParameters)
{
    char *line;
    char *argv[20];
    char hint[] = USER_NAME "@" USER_NAME "-STM32:~$ ";
    ssize_t len;

    fio_printf(1, "\rWelcome to FreeRTOS Shell\r\n");
    while(1){
        line = linenoise(hint , &len);
        if (len > 0) {
            linenoiseHistoryAdd(line); /* Add to the history. */
            int n = parse_command_args(line, argv);
            int cmdid = find_command_id(argv[0]);
            if(cmdid != -1){ // FIXME: proper macro to identify error type
                fio_printf(1, "\r\n"); /* output starts at new line */
                cmd_list[cmdid].fptr(n, argv);
            }else{
                fio_printf(2, "\r\n\"%s\" command not found.\r\n", line);
            }
            vPortFree(line);
        }else{
            fio_printf(2, "\r\n");
        }
    }
}

