/* Fork from linenoise -- guerrilla line editing library against the idea that a
 * line editing lib needs to be 20,000 lines of C code.
 *
 * You can find the latest source code at:
 *
 * http://github.com/antirez/linenoise
 *
 * Does a number of crazy assumptions that happen to be true in 99.9999% of
 * the 2010 UNIX computers around.
 *
 * ------------------------------------------------------------------------
 *
 * Copyright (c) 2010-2014, Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2010-2013, Pieter Noordhuis <pcnoordhuis at gmail dot com>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <FreeRTOS.h>
#include "linenoise.h"

#define LINENOISE_DEFAULT_HISTORY_MAX_LEN 10
#define LINENOISE_MAX_LINE                61
#define STDIN_FILENO 0
#define STDOUT_FILENO 1

static linenoiseCompletionCallback *completionCallback = NULL;
static int mlmode = 0; /* Multi line mode. Default is single line. */
static int history_max_len = LINENOISE_DEFAULT_HISTORY_MAX_LEN;
static int history_len = 0;
static char **history = NULL;

struct linenoiseState {
    int ifd; /* Terminal stdin file descriptor. */
    int ofd; /* Terminal stdout file descriptor. */
    char *buf; /* Edited line buffer. */
    size_t buflen; /* Edited line buffer size. */
    const char *prompt; /* Prompt to display. */
    size_t plen; /* Prompt length. */
    size_t pos; /* Current cursor position. */
    size_t oldpos; /* Previous refresh cursor position. */
    size_t len; /* Current edited line length. */
    size_t cols; /* Number of columns in terminal. */
    size_t maxrows; /* Maximum num of rows used so far (multiline mode) */
    int history_index; /* The history index we are currently editing. */
};

enum KeyName{
    NULL_CHAR = 0,
    CTRL_A = 1,  // Home
    CTRL_B = 2,  // Back character
    CTRL_C = 3,  // SIGINT
    CTRL_D = 4,  // EOF
    CTRL_E = 5,  // End
    CTRL_F = 6,  // Forward Charater
    CTRL_H = 8,  // Backspace
    TAB    = 9,  // Tab (C-i)
    LF     = 10, // Line feed, Also C-j
    CTRL_K = 11, // Delete from current to end of line.
    CTRL_L = 12, // Clear
    CR     = 13, // Carriage return, Also C-m
    CTRL_N = 14, // Next command (Down)
    CTRL_P = 16, // Previous command (Up)
    CTRL_Q = 17, // Allow output
    CTRL_R = 18, // Backward search
    CTRL_S = 19, // Stop output, forward search
    CTRL_T = 20, // Swap last two character
    CTRL_U = 21, /* Clear line */
    CTRL_W = 23, /* Backward delete a word */
    CTRL_Z = 26, // SIGTSTP
    ESC    = 27,
    BACKSPACE = 127,
};

int linenoiseHistoryAdd(const char *line);
static void refreshLine(struct linenoiseState *l);

/* warpper for pvPortMalloc(), return a magic number if try to allocate 0 byte */
static void *__malloc(size_t size){
    if(size) return pvPortMalloc(size);
    else return (void *)0x1;
}

static void __free(void *ptr){
    if(ptr != (void *)0x1) vPortFree(ptr);
    /* if equal to 0x1, nothing to do */
}

/* Set if to use or not the multi line mode. */
void linenoiseSetMultiLine(int ml) {
    mlmode = ml;
}

/* Clear the screen. Used to handle ctrl+l */
void linenoiseClearScreen(void) {
    if (fio_write(1,"\x1b[H\x1b[2J",7) <= 0) {
        /* nothing to do, just to avoid warning. */
    }
}

static void linenoiseBeep(void) { }

static void __freeCompletions(linenoiseCompletions *lc) {
    size_t i;
    for (i = 0; i < lc->len; i++)
        __free(lc->cvec[i]);
    if (lc->cvec != NULL)
        __free(lc->cvec);
}

/* This is an helper function for linenoiseEdit() and is called when the
 * user types the <tab> key in order to complete the string currently in the
 * input.
 *
 * The state of the editing is encapsulated into the pointed linenoiseState
 * structure as described in the structure definition. */
static int completeLine(struct linenoiseState *ls) {
    linenoiseCompletions lc = { 0, NULL };
    int nread, nwritten;
    char c = 0;
    completionCallback(ls->buf,&lc);
    if (lc.len == 0) {
        linenoiseBeep();
    } else {
        size_t stop = 0, i = 0;
        while(!stop) {
            /* Show completion or original buffer */
            if (i < lc.len) {
                struct linenoiseState saved = *ls;
                ls->len = ls->pos = strlen(lc.cvec[i]);
                ls->buf = lc.cvec[i];
                refreshLine(ls);
                ls->len = saved.len;
                ls->pos = saved.pos;
                ls->buf = saved.buf;
            } else {
                refreshLine(ls);
            }
            nread = fio_read(ls->ifd,&c,1);
            if (nread <= 0) {
                __freeCompletions(&lc);
                return -1;
            }
            switch(c) {
                case 9: /* tab */
                    i = (i+1) % (lc.len+1);
                    if (i == lc.len) linenoiseBeep();
                    break;
                case 27: /* escape */
                    /* Re-show original buffer */
                    if (i < lc.len) refreshLine(ls);
                    stop = 1;
                    break; default:
                    /* Update buffer and return */
                    if (i < lc.len) {
                        nwritten = snprintf(ls->buf,ls->buflen,"%s",lc.cvec[i]);
                        ls->len = ls->pos = nwritten;
                    }
                    stop = 1;
                    break;
            }
        }
    }
    __freeCompletions(&lc);
    return c; /* Return last read character */
}

/* Register a callback function to be called for tab-completion. */
void linenoiseSetCompletionCallback(linenoiseCompletionCallback *fn) {
    completionCallback = fn;
}
/* This function is used by the callback function registered by the user
 * in order to add completion options given the input string when the
 * user typed <tab>. See the example.c source code for a very easy to
 * understand example. */
void linenoiseAddCompletion(linenoiseCompletions *lc, const char *str) {
    size_t len = strlen(str);
    char *copy, **cvec;
    copy = __malloc(len+1);
    if (copy == NULL) return;
    memcpy(copy,str,len+1);
    __free(lc->cvec);
    cvec = __malloc(sizeof(char*)*(lc->len+1));
    if (cvec == NULL) {
        __free(copy);
        return;
    }
    lc->cvec = cvec;
    lc->cvec[lc->len++] = copy;
}
/* =========================== Line editing ================================= */
/* We define a very simple "append buffer" structure, that is an heap
 * allocated string where we can append to. This is useful in order to
 * write all the escape sequences in a buffer and flush them to the standard
 * output in a single call, to avoid flickering effects. */
struct abuf {
    char *b;
    int len;
};
static void abInit(struct abuf *ab) {
    ab->b = NULL;
    ab->len = 0;
}
static void abAppend(struct abuf *ab, const char *s, int len) {
    __free(ab->b);
    char *new = __malloc(ab->len+len);
    if (new == NULL) return;
    memcpy(new+ab->len,s,len);
    ab->b = new;
    ab->len += len;
}
static void abFree(struct abuf *ab) {
    __free(ab->b);
}
/* Single line low level line refresh.
 *
 * Rewrite the currently edited line accordingly to the buffer content,
 * cursor position, and number of columns of the terminal. */
static void refreshSingleLine(struct linenoiseState *l) {
    size_t plen = strlen(l->prompt);
    int fd = l->ofd;
    char *buf = l->buf;
    size_t len = l->len;
    size_t pos = l->pos;
    while((plen+pos) >= l->cols) {
        buf++;
        len--;
        pos--;
    }
    while (plen+len > l->cols) {
        len--;
    }
    // Clear last line
    fio_write(fd, "\x1b[2K\r", 5);
    fio_write(fd, l->prompt, plen);
    fio_write(fd, buf, len);
    /* Move cursor to original position. */
    fio_printf(fd, "\r\x1b[%dC", (int)(pos+plen));
}
/* Multi line low level line refresh.
 *
 * Rewrite the currently edited line accordingly to the buffer content,
 * cursor position, and number of columns of the terminal. */
static void refreshMultiLine(struct linenoiseState *l) {
    char seq[64];
    int plen = strlen(l->prompt);
    int rows = (plen+l->len+l->cols-1)/l->cols; /* rows used by current buf. */
    int rpos = (plen+l->oldpos+l->cols)/l->cols; /* cursor relative row. */
    int rpos2; /* rpos after refresh. */
    int col; /* colum position, zero-based. */
    int old_rows = l->maxrows;
    int fd = l->ofd, j;
    struct abuf ab;
    /* Update maxrows if needed. */
    if (rows > (int)l->maxrows) l->maxrows = rows;
    /* First step: clear all the lines used before. To do so start by
     * going to the last row. */
    abInit(&ab);
    if (old_rows-rpos > 0) {
        snprintf(seq,64,"\x1b[%dB", old_rows-rpos);
        abAppend(&ab,seq,strlen(seq));
    }
    /* Now for every row clear it, go up. */
    for (j = 0; j < old_rows-1; j++) {
        snprintf(seq,64,"\r\x1b[0K\x1b[1A");
        abAppend(&ab,seq,strlen(seq));
    }
    /* Clean the top line. */
    snprintf(seq,64,"\r\x1b[0K");
    abAppend(&ab,seq,strlen(seq));
    /* Write the prompt and the current buffer content */
    abAppend(&ab,l->prompt,strlen(l->prompt));
    abAppend(&ab,l->buf,l->len);
    /* If we are at the very end of the screen with our prompt, we need to
     * emit a newline and move the prompt to the first column. */
    if (l->pos &&
            l->pos == l->len &&
            (l->pos+plen) % l->cols == 0)
    {
        abAppend(&ab,"\n",1);
        snprintf(seq,64,"\r");
        abAppend(&ab,seq,strlen(seq));
        rows++;
        if (rows > (int)l->maxrows) l->maxrows = rows;
    }
    /* Move cursor to right position. */
    rpos2 = (plen+l->pos+l->cols)/l->cols; /* current cursor relative row. */
    /* Go up till we reach the expected positon. */
    if (rows-rpos2 > 0) {
        snprintf(seq,64,"\x1b[%dA", rows-rpos2);
        abAppend(&ab,seq,strlen(seq));
    }
    /* Set column. */
    col = (plen+(int)l->pos) % (int)l->cols;
    if (col)
        snprintf(seq,64,"\r\x1b[%dC", col);
    else
        snprintf(seq,64,"\r");
    abAppend(&ab,seq,strlen(seq));
    l->oldpos = l->pos;
    if (fio_write(fd,ab.b,ab.len) == -1) {} /* Can't recover from write error. */
    abFree(&ab);
}
/* Calls the two low level functions refreshSingleLine() or
 * refreshMultiLine() according to the selected mode. */
static void refreshLine(struct linenoiseState *l) {
    if (mlmode)
        refreshMultiLine(l);
    else
        refreshSingleLine(l);
}
/* Insert the character 'c' at cursor current position.
 *
 * On error writing to the terminal -1 is returned, otherwise 0. */
int linenoiseEditInsert(struct linenoiseState *l, char c) {
    if (l->len < l->buflen) {
        if (l->len == l->pos) {
            l->buf[l->pos] = c;
            l->pos++;
            l->len++;
            l->buf[l->len] = '\0';
            if ((!mlmode && l->plen+l->len < l->cols) /* || mlmode */) {
                /* Avoid a full update of the line in the
                 * trivial case. */
                if (fio_write(l->ofd,&c,1) == -1) return -1;
            } else {
                refreshLine(l);
            }
        } else {
            memmove(l->buf+l->pos+1,l->buf+l->pos,l->len-l->pos);
            l->buf[l->pos] = c;
            l->len++;
            l->pos++;
            l->buf[l->len] = '\0';
            refreshLine(l);
        }
    }
    return 0;
}
/* Move cursor on the left. */
void linenoiseEditMoveLeft(struct linenoiseState *l) {
    if (l->pos > 0) {
        l->pos--;
        refreshLine(l);
    }
}
/* Move cursor on the right. */
void linenoiseEditMoveRight(struct linenoiseState *l) {
    if (l->pos != l->len) {
        l->pos++;
        refreshLine(l);
    }
}
/* Move cursor to the start of the line. */
void linenoiseEditMoveHome(struct linenoiseState *l) {
    if (l->pos != 0) {
        l->pos = 0;
        refreshLine(l);
    }
}
/* Move cursor to the end of the line. */
void linenoiseEditMoveEnd(struct linenoiseState *l) {
    if (l->pos != l->len) {
        l->pos = l->len;
        refreshLine(l);
    }
}
/* Substitute the currently edited line with the next or previous history
 * entry as specified by 'dir'. */
#define LINENOISE_HISTORY_NEXT 0
#define LINENOISE_HISTORY_PREV 1
void linenoiseEditHistoryNext(struct linenoiseState *l, int dir) {
    if (history_len > 1) {
        /* Update the current history entry before to
         * overwrite it with the next one. */
        __free(history[history_len - 1 - l->history_index]);
        size_t buflen = strlen(l->buf);
        history[history_len - 1 - l->history_index] = __malloc(buflen);
        strncpy(history[history_len - 1 - l->history_index], l->buf, buflen);
        /* Show the new entry */
        l->history_index += (dir == LINENOISE_HISTORY_PREV) ? 1 : -1;
        if (l->history_index < 0) {
            l->history_index = 0;
            return;
        } else if (l->history_index >= history_len) {
            l->history_index = history_len-1;
            return;
        }
        strncpy(l->buf,history[history_len - 1 - l->history_index],l->buflen);
        l->buf[l->buflen-1] = '\0';
        l->len = l->pos = strlen(l->buf);
        refreshLine(l);
    }
}
/* Delete the character at the right of the cursor without altering the cursor
 * position. Basically this is what happens with the "Delete" keyboard key. */
void linenoiseEditDelete(struct linenoiseState *l) {
    if (l->len > 0 && l->pos < l->len) {
        memmove(l->buf+l->pos,l->buf+l->pos+1,l->len-l->pos-1);
        l->len--;
        l->buf[l->len] = '\0';
        refreshLine(l);
    }
}
/* Backspace implementation. */
void linenoiseEditBackspace(struct linenoiseState *l) {
    if (l->pos > 0 && l->len > 0) {
        memmove(l->buf+l->pos-1,l->buf+l->pos,l->len-l->pos);
        l->pos--;
        l->len--;
        l->buf[l->len] = '\0';
        refreshLine(l);
    }
}
/* Delete the previosu word, maintaining the cursor at the start of the
 * current word. */
void linenoiseEditDeletePrevWord(struct linenoiseState *l) {
    size_t old_pos = l->pos;
    size_t diff;
    while (l->pos > 0 && l->buf[l->pos-1] == ' ')
        l->pos--;
    while (l->pos > 0 && l->buf[l->pos-1] != ' ')
        l->pos--;
    diff = old_pos - l->pos;
    memmove(l->buf+l->pos,l->buf+old_pos,l->len-old_pos+1);
    l->len -= diff;
    refreshLine(l);
}
/* This function is the core of the line editing capability of linenoise.
 * It expects 'fd' to be already in "raw mode" so that every key pressed
 * will be returned ASAP to read().
 *
 * The resulting string is put into 'buf' when the user type enter, or
 * when ctrl+d is typed.
 *
 * The function returns the length of the current buffer. */
static int linenoiseEdit(int stdin_fd, int stdout_fd, char *buf, size_t buflen, const char *prompt)
{
    struct linenoiseState l;
    /* Populate the linenoise state that we pass to functions implementing
     * specific editing functionalities. */
    l.ifd = stdin_fd;
    l.ofd = stdout_fd;
    l.buf = buf;
    l.buflen = buflen;
    l.prompt = prompt;
    l.plen = strlen(prompt);
    l.oldpos = l.pos = 0;
    l.len = 0;
    l.cols = 80;
    l.maxrows = 0;
    l.history_index = 0;
    /* Buffer starts empty. */
    l.buf[0] = '\0';
    l.buflen--; /* Make sure there is always space for the nulterm */
    /* The latest history entry is always our current buffer, that
     * initially is just an empty string. */
    linenoiseHistoryAdd("");
    if (fio_write(l.ofd,prompt,l.plen) == -1) return -1;
    while(1) {
        char c;
        int nread;
        char seq[3];
        nread = fio_read(l.ifd,&c,1);
        if (nread <= 0) return l.len;
        /* Only autocomplete when the callback is set. It returns < 0 when
         * there was an error reading from fd. Otherwise it will return the
         * character that should be handled next. */
        if (c == 9 && completionCallback != NULL) {
            c = completeLine(&l);
            /* Return on errors */
            if (c < 0) return l.len;
            /* Read next character when 0 */
            if (c == 0) continue;
        }
        switch(c) {
            case CR: /* enter */
                history_len--;
                __free(history[history_len]);
                if (mlmode) linenoiseEditMoveEnd(&l);
                return (int)l.len;
            case CTRL_C: /* ctrl-c */
                return -1;
            case BACKSPACE: /* backspace */
            case CTRL_H: /* ctrl-h */
                linenoiseEditBackspace(&l);
                break;
            case CTRL_D: /* ctrl-d, remove char at right of cursor, or if the
                            line is empty, act as end-of-file. */
                if (l.len > 0) {
                    linenoiseEditDelete(&l);
                } else {
                    history_len--;
                    __free(history[history_len]);
                    return -1;
                }
                break;
            case CTRL_T: /* ctrl-t, swaps current character with previous. */
                if (l.pos > 0 && l.pos < l.len) {
                    int aux = buf[l.pos-1];
                    buf[l.pos-1] = buf[l.pos];
                    buf[l.pos] = aux;
                    if (l.pos != l.len-1) l.pos++;
                    refreshLine(&l);
                }
                break;
            case CTRL_B: /* ctrl-b */
                linenoiseEditMoveLeft(&l);
                break;
            case CTRL_F: /* ctrl-f */
                linenoiseEditMoveRight(&l);
                break;
            case CTRL_P: /* ctrl-p */
                linenoiseEditHistoryNext(&l, LINENOISE_HISTORY_PREV);
                break;
            case CTRL_N: /* ctrl-n */
                linenoiseEditHistoryNext(&l, LINENOISE_HISTORY_NEXT);
                break;
            case ESC: /* escape sequence */
                /* Read the next two bytes representing the escape sequence.
                 * Use two calls to handle slow terminals returning the two
                 * chars at different times. */
                if (fio_read(l.ifd,seq,1) == -1) break;
                if (fio_read(l.ifd,seq+1,1) == -1) break;
                /* ESC [ sequences. */
                if (seq[0] == '[') {
                    if (seq[1] >= '0' && seq[1] <= '9') {
                        /* Extended escape, read additional byte. */
                        if (fio_read(l.ifd,seq+2,1) == -1) break;
                        if (seq[2] == '~') {
                            switch(seq[1]) {
                                case '3': /* Delete key. */
                                    linenoiseEditDelete(&l);
                                    break;
                            }
                        }
                    } else {
                        switch(seq[1]) {
                            case 'A': /* Up */
                                linenoiseEditHistoryNext(&l, LINENOISE_HISTORY_PREV);
                                break;
                            case 'B': /* Down */
                                linenoiseEditHistoryNext(&l, LINENOISE_HISTORY_NEXT);
                                break;
                            case 'C': /* Right */
                                linenoiseEditMoveRight(&l);
                                break;
                            case 'D': /* Left */
                                linenoiseEditMoveLeft(&l);
                                break;
                            case 'H': /* Home */
                                linenoiseEditMoveHome(&l);
                                break;
                            case 'F': /* End*/
                                linenoiseEditMoveEnd(&l);
                                break;
                        }
                    }
                }
                /* ESC O sequences. */
                else if (seq[0] == 'O') {
                    switch(seq[1]) {
                        case 'H': /* Home */
                            linenoiseEditMoveHome(&l);
                            break;
                        case 'F': /* End*/
                            linenoiseEditMoveEnd(&l);
                            break;
                    }
                }
                break;
            default:
                if (linenoiseEditInsert(&l,c)) return -1;
                break;
            case CTRL_U: /* Ctrl+u, delete the whole line. */
                buf[0] = '\0';
                l.pos = l.len = 0;
                refreshLine(&l);
                break;
            case CTRL_K: /* Ctrl+k, delete from current to end of line. */
                buf[l.pos] = '\0';
                l.len = l.pos;
                refreshLine(&l);
                break;
            case CTRL_A: /* Ctrl+a, go to the start of the line */
                linenoiseEditMoveHome(&l);
                break;
            case CTRL_E: /* ctrl+e, go to the end of the line */
                linenoiseEditMoveEnd(&l);
                break;
            case CTRL_L: /* ctrl+l, clear screen */
                linenoiseClearScreen();
                refreshLine(&l);
                break;
            case CTRL_W: /* ctrl+w, delete previous word */
                linenoiseEditDeletePrevWord(&l);
                break;
        }
    }
    return l.len;
}
/* This special mode is used by linenoise in order to print scan codes
 * on screen for debugging / development purposes. It is implemented
 * by the linenoise_example program using the --keycodes option. */
void linenoisePrintKeyCodes(void) {
    char quit[4];
    memset(quit,' ',4);
    while(1) {
        char c;
        int nread;
        nread = fio_read(STDIN_FILENO,&c,1);
        if (nread <= 0) continue;
        memmove(quit,quit+1,sizeof(quit)-1); /* shift string to left. */
        quit[sizeof(quit)-1] = c; /* Insert current char on the right. */
        if (memcmp(quit,"quit",sizeof(quit)) == 0) break;
    }
}
/* This function calls the line editing function linenoiseEdit() using
 * the STDIN file descriptor set in raw mode. */
static int linenoiseRaw(char *buf, size_t buflen, const char *prompt) {
    int count;
    if (buflen == 0) {
        return -1;
    }
    /* Interactive editing. */
    count = linenoiseEdit(STDIN_FILENO, STDOUT_FILENO, buf, buflen, prompt);
    return count;
}
/* The high level function that is the main API of the linenoise library.
 * This function checks if the terminal has basic capabilities, just checking
 * for a blacklist of stupid terminals, and later either calls the line
 * editing function or uses dummy fgets() so that you will be able to type
 * something even in the most desperate of the conditions. */
char *linenoise(const char *prompt, ssize_t *count) {
    char buf[LINENOISE_MAX_LINE];

    *count = linenoiseRaw(buf,LINENOISE_MAX_LINE,prompt);
    if (*count <= 0) return NULL;
    char *ret = __malloc(*count);
    strncpy(ret, buf, *count);
    ret[*count] = 0;
    return ret;
}
/* ================================ History ================================= */
/* This is the API call to add a new entry in the linenoise history.
 * It uses a fixed array of char pointers that are shifted (memmoved)
 * when the history max length is reached in order to remove the older
 * entry and make room for the new one, so it is not exactly suitable for huge
 * histories, but will work well for a few hundred of entries.
 *
 * Using a circular buffer is smarter, but a bit more complex to handle. */
int linenoiseHistoryAdd(const char *line) {
    char *linecopy;
    if (history_max_len == 0) return 0;
    /* Initialization on first call. */
    if (history == NULL) {
        history = __malloc(sizeof(char*)*history_max_len);
        if (history == NULL) return 0;
        memset(history,0,(sizeof(char*)*history_max_len));
    }
    /* Don't add duplicated lines. */
    if (history_len && !strcmp(history[history_len-1], line)) return 0;
    /* Add an heap allocated copy of the line in the history.
     * If we reached the max length, remove the older line. */
    size_t linelen = strlen(line);
    linecopy = __malloc(linelen);
    strncpy(linecopy, line, linelen);
    if (!linecopy) return 0;
    if (history_len == history_max_len) {
        __free(history[0]);
        memmove(history,history+1,sizeof(char*)*(history_max_len-1));
        history_len--;
    }
    history[history_len] = linecopy;
    history_len++;
    return 1;
}
/* Set the maximum length for the history. This function can be called even
 * if there is already some history, the function will make sure to retain
 * just the latest 'len' elements if the new history length value is smaller
 * than the amount of items already inside the history. */
int linenoiseHistorySetMaxLen(int len) {
    char **new;
    if (len < 1) return 0;
    if (history) {
        int tocopy = history_len;
        new = __malloc(sizeof(char*)*len);
        if (new == NULL) return 0;
        /* If we can't copy everything, __free the elements we'll not use. */
        if (len < tocopy) {
            int j;
            for (j = 0; j < tocopy-len; j++) __free(history[j]);
            tocopy = len;
        }
        memset(new,0,sizeof(char*)*len);
        memcpy(new,history+(history_len-tocopy), sizeof(char*)*tocopy);
        __free(history);
        history = new;
    }
    history_max_len = len;
    if (history_len > history_max_len)
        history_len = history_max_len;
    return 1;
}
#if 0 /* Not using this feature at this time */
/* Save the history in the specified file. On success 0 is returned
 * otherwise -1 is returned. */
int linenoiseHistorySave(const char *filename) {
    FILE *fp = fopen(filename,"w");
    int j;
    if (fp == NULL) return -1;
    for (j = 0; j < history_len; j++)
        fprintf(fp,"%s\n",history[j]);
    fclose(fp);
    return 0;
}
/* Load the history from the specified file. If the file does not exist
 * zero is returned and no operation is performed.
 *
 * If the file exists and the operation succeeded 0 is returned, otherwise
 * on error -1 is returned. */
int linenoiseHistoryLoad(const char *filename) {
    FILE *fp = fopen(filename,"r");
    char buf[LINENOISE_MAX_LINE];
    if (fp == NULL) return -1;
    while (fgets(buf,LINENOISE_MAX_LINE,fp) != NULL) {
        char *p;
        p = strchr(buf,'\r');
        if (!p) p = strchr(buf,'\n');
        if (p) *p = '\0';
        linenoiseHistoryAdd(buf);
    }
    fclose(fp);
    return 0;
}
/* Free the history, but does not reset it. Only used when we have to
 * exit() to avoid memory leaks are reported by valgrind & co. */
static void __freeHistory(void) {
    if (history) {
        int j;
        for (j = 0; j < history_len; j++)
            __free(history[j]);
        __free(history);
    }
}
#endif
