#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <time.h>

#define CTRL_KEY(k) ((k) & 0x1f)
#define KILO_TAB_STOP 8 

void editorInsertNewline();
void editorInsertChar(int c);
enum editorKey {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN
};

typedef struct erow {
    int size;
    char *chars;

    int rsize;
    char *render;
} erow;

struct editorConfig{
    int cx, cy;
    int rx;

    int rowoff;
    int coloff;

    int screenrows;
    int screencols;

    int numrows;
    erow *row;

    char *filename;

    char statusmsg[80];
    time_t statusmsg_time;

    HANDLE hInput; 
    HANDLE hOutput;

    DWORD originalMode;
};

struct editorConfig E;

/*** terminal ***/

void disableRawMode()
{
    SetConsoleMode(E.hInput, E.originalMode);
}

void enableRawMode()
{
    E.hInput = GetStdHandle(STD_INPUT_HANDLE);
    E.hOutput = GetStdHandle(STD_OUTPUT_HANDLE);

    GetConsoleMode(E.hInput, &E.originalMode);

    DWORD raw = E.originalMode;
    raw &= ~(ENABLE_ECHO_INPUT |
             ENABLE_LINE_INPUT |
             ENABLE_PROCESSED_INPUT);

    SetConsoleMode(E.hInput, raw);

    DWORD mode;
    GetConsoleMode(E.hOutput, &mode);
    SetConsoleMode(E.hOutput, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    atexit(disableRawMode);
}

/*** input ***/

int editorReadKey() {
    INPUT_RECORD record;
    DWORD read;

    while (1) {
        ReadConsoleInput(E.hInput, &record, 1, &read);

        if (record.EventType == KEY_EVENT &&
            record.Event.KeyEvent.bKeyDown)
        {
            KEY_EVENT_RECORD key = record.Event.KeyEvent;

            switch (key.wVirtualKeyCode)
            {
                case VK_LEFT:  return ARROW_LEFT;
                case VK_RIGHT: return ARROW_RIGHT;
                case VK_UP:    return ARROW_UP;
                case VK_DOWN:  return ARROW_DOWN;
            }

            if (key.uChar.AsciiChar != 0)
                return key.uChar.AsciiChar;
        }
    }
}

void editorMoveCursor(int key)
{
    switch (key)
    {
        case ARROW_LEFT:
            if (E.cx != 0) {
                E.cx--;
            } else if (E.cy > 0) {
                E.cy--;
                E.cx = E.row[E.cy].size;
            }
            break;

        case ARROW_RIGHT:
            if (E.cy < E.numrows) {
                if (E.cx < E.row[E.cy].size) {
                    E.cx++;
                } else if (E.cx == E.row[E.cy].size) {
                    E.cy++;
                    E.cx = 0;
                }
            }
            break;

        case ARROW_UP:
            if (E.cy != 0) {
                E.cy--;
            }
            break;

        case ARROW_DOWN:
            if (E.cy < E.numrows - 1) {
                E.cy++;
            }
            break;
    }

    int rowlen = (E.cy >= E.numrows) ? 0 : E.row[E.cy].size;
    if (E.cx > rowlen) {
        E.cx = rowlen;
    }
}

void editorProcessKeypress()
{
    int c = editorReadKey();

    switch (c) {
        case CTRL_KEY('q'):
            exit(0);
            break;
        case '\r': //ENTER
            editorInsertNewline();
            break;
        
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;

        default:
            if (!iscntrl(c))
            {
                editorInsertChar(c);
            }
            break;
    }

    snprintf(E.statusmsg, sizeof(E.statusmsg),
             "cx=%d cy=%d", E.cx, E.cy);
    E.statusmsg_time = time(NULL);
}

/*** rows ***/

void editorUpdateRow(erow *row)
{
    int tabs = 0;
    for (int j = 0; j < row->size; j++)
        if (row->chars[j] == '\t') tabs++;

    free(row->render);
    row->render = malloc(row->size + tabs * (KILO_TAB_STOP - 1) + 1);

    int idx = 0;
    for (int j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') {
            row->render[idx++] = ' ';
            while (idx % KILO_TAB_STOP != 0)
                row->render[idx++] = ' ';
        } else {
            row->render[idx++] = row->chars[j];
        }
    }

    row->render[idx] = '\0';
    row->rsize = idx;
}

void editorAppendRow(char *s, size_t len)
{
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

    int at = E.numrows;
    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);

    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';

    E.row[at].rsize = 0;
    E.row[at].render = NULL;

    editorUpdateRow(&E.row[at]);

    E.numrows++;
}

/*** file i/o ***/

void editorOpen(const char *filename)
{
    FILE *fp = fopen(filename, "r");
    if (!fp) return;

    E.filename = _strdup(filename);

    char line[1024];

    while (fgets(line, sizeof(line), fp)) {
        int len = strlen(line);

        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            len--;

        editorAppendRow(line, len);
    }

    fclose(fp);
}

/*** editor operations ***/

int editorRowCxToRx(erow *row, int cx)
{
    int rx = 0;
    for (int j = 0; j < cx; j++) {
        if (row->chars[j] == '\t')
            rx += (KILO_TAB_STOP - 1) - (rx % KILO_TAB_STOP);
        rx++;
    }
    return rx;
}

void editorScroll()
{
    if (E.cy < E.numrows) {
        E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
    } else {
        E.rx = E.cx;
    }

    if (E.cy < E.rowoff)
        E.rowoff = E.cy;

    if (E.cy >= E.rowoff + E.screenrows)
        E.rowoff = E.cy - E.screenrows + 1;

    if (E.rx < E.coloff)
        E.coloff = E.rx;

    if (E.rx >= E.coloff + E.screencols)
        E.coloff = E.rx - E.screencols + 1;
}

/* Input */

void editorRowInsertChar(erow *row, int at, int c)
{
    if (at < 0 || at > row->size) at = row->size;

    row -> chars = realloc(row->chars,row->size+2);
    memmove(&row->chars[at+1],&row->chars[at],row->size - at + 1);

    row -> size++;
    row->chars[at] = c;

    editorUpdateRow(row);
}

void editorInsertNewline()
{
    if (E.cx == 0) {
        E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

        memmove(&E.row[E.cy + 1], &E.row[E.cy],
                sizeof(erow) * (E.numrows - E.cy));

        E.row[E.cy].size = 0;
        E.row[E.cy].chars = malloc(1);
        E.row[E.cy].chars[0] = '\0';

        E.row[E.cy].rsize = 0;
        E.row[E.cy].render = NULL;

        editorUpdateRow(&E.row[E.cy]);

        E.numrows++;
    } 
    else {
        // DO NOT take pointer before realloc
        int cx = E.cx;
        int cy = E.cy;

        E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

        memmove(&E.row[cy + 2], &E.row[cy + 1],
                sizeof(erow) * (E.numrows - cy - 1));

        // NOW safe to access rows again
        erow *row = &E.row[cy];

        // new row (right side)
        E.row[cy + 1].size = row->size - cx;
        E.row[cy + 1].chars = malloc(E.row[cy + 1].size + 1);

        memcpy(E.row[cy + 1].chars,
               &row->chars[cx],
               E.row[cy + 1].size);

        E.row[cy + 1].chars[E.row[cy + 1].size] = '\0';
        E.row[cy + 1].render = NULL;
        E.row[cy + 1].rsize = 0;

        editorUpdateRow(&E.row[cy + 1]);

        // shrink original row
        row->size = cx;
        row->chars[cx] = '\0';

        editorUpdateRow(row);

        E.numrows++;
    }

    E.cy++;
    E.cx = 0;
}

void editorInsertChar(int c)
{
    if (E.cy == E.numrows) {
        editorAppendRow("", 0);
    }

    editorRowInsertChar(&E.row[E.cy], E.cx, c);
    E.cx++;
}
/*** output ***/
void editorDrawRows()
{
    for (int y = 0; y < E.screenrows; y++) {
        int filerow = y + E.rowoff;

        if (filerow >= E.numrows) {
            WriteConsoleA(E.hOutput, "~\r\n", 3, NULL, NULL);
        } else {
            int len = E.row[filerow].rsize - E.coloff;
            if (len < 0) len = 0;
            if (len > E.screencols) len = E.screencols;

            WriteConsoleA(E.hOutput,
                &E.row[filerow].render[E.coloff],
                len, NULL, NULL);

            WriteConsoleA(E.hOutput, "\r\n", 2, NULL, NULL);
        }
    }
}

void editorDrawStatusBar() {
    printf("\x1b[7m");

    char status[80];
    int len = snprintf(status, sizeof(status),
        "%.20s - %d lines",
        E.filename ? E.filename : "[No Name]",
        E.numrows);

    if (len > E.screencols) len = E.screencols;
    fwrite(status, 1, len, stdout);

    while (len < E.screencols) {
        putchar(' ');
        len++;
    }

    printf("\x1b[m");
    printf("\r\n");

    int msglen = E.statusmsg[0] ? strlen(E.statusmsg) : 0;
    if (msglen > E.screencols) msglen = E.screencols;
    fwrite(E.statusmsg, 1, msglen, stdout);
}

void editorRefreshScreen()
{
    editorScroll();

    printf("\x1b[H");

    editorDrawRows();
    editorDrawStatusBar();

    int cx = (E.rx - E.coloff) + 1;
    int cy = (E.cy - E.rowoff) + 1;

    printf("\x1b[%d;%dH", cy, cx);
}

/*** init ***/

int getWindowSize(int *rows, int *cols)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;

    if (!GetConsoleScreenBufferInfo(E.hOutput, &csbi)) {
        return -1;
    }

    *cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    *rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;

    return 0;
}

void initEditor()
{
    E.cx = 0;
    E.cy = 0;

    E.rowoff = 0;
    E.coloff = 0;

    E.numrows = 0;
    E.row = NULL;

    E.filename = NULL;
    E.statusmsg[0] = '\0';
    E.statusmsg_time = 0;

    if (getWindowSize(&E.screenrows, &E.screencols) == -1) {
        exit(1);
    }

    E.screenrows -= 2;
}

/*** main ***/

int main(int argc, char *argv[])
{
    enableRawMode();
    initEditor();

    if (argc >= 2) {
        editorOpen(argv[1]);
    }

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}