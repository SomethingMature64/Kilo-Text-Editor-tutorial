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
void editorSave();
void editorFind();

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

struct abuf{
    char *b;
    int len;
};

void abAppend(struct abuf *ab, const char *s, int len)
{
    char *new = realloc(ab->b, ab->len + len);

    if (new == NULL) return;

    memcpy(&new[ab->len],s,len);
    ab->b = new;
    ab->len += len;
    
}

void abFree(struct abuf *ab)
{
    free(ab->b);
}

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
    HANDLE hBackBuffer;
    HANDLE hFrontBuffer;

    DWORD originalMode;
};

struct editorConfig E;

/*** terminal ***/

void disableRawMode()
{
    SetConsoleMode(E.hInput, E.originalMode);
    if (E.hBackBuffer != NULL && E.hBackBuffer != E.hFrontBuffer) {
        CloseHandle(E.hBackBuffer);
    }
}

void enableRawMode()
{
    E.hInput = GetStdHandle(STD_INPUT_HANDLE);
    E.hOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    E.hFrontBuffer = E.hOutput;
    
    // Create back buffer
    E.hBackBuffer = CreateConsoleScreenBuffer(
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        CONSOLE_TEXTMODE_BUFFER,
        NULL);

    GetConsoleMode(E.hInput, &E.originalMode);

    DWORD raw = E.originalMode;
    raw &= ~(ENABLE_ECHO_INPUT |
             ENABLE_LINE_INPUT |
             ENABLE_PROCESSED_INPUT);

    SetConsoleMode(E.hInput, raw);

    DWORD mode;
    GetConsoleMode(E.hBackBuffer, &mode);
    SetConsoleMode(E.hBackBuffer, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    atexit(disableRawMode);
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

void editorDelRow(int at)
{
    if (at < 0 || at >= E.numrows) return;

    free(E.row[at].chars);
    free(E.row[at].render);

    memmove(&E.row[at], &E.row[at+1], sizeof(erow)*(E.numrows-at-1));

    E.numrows--;
    
}

void editorRowDelChar(erow *row, int at)
{
    if (at < 0 || at >= row->size) return;

    memmove(&row->chars[at],&row->chars[at+1],
    row->size - at);

    row->size--;

    editorUpdateRow(row);
}

void editorDelChar()
{
    if (E.cy == E.numrows) return;
    if (E.cx == 0 && E.cy == 0) return;

    erow *row = &E.row[E.cy];

    if (E.cx >0)
    {
        //Case 1: Delete within line
        editorRowDelChar(row,E.cx-1);
        E.cx--;
    } else
    {
        // Case 2: merge with previous line
        int prev_len = E.row[E.cy - 1].size;

        //expand previous row
        E.row[E.cy-1].chars=realloc(
            E.row[E.cy-1].chars,prev_len+row->size+1
        );

        memcpy(&E.row[E.cy-1].chars[prev_len], row->chars, row->size);

        E.row[E.cy - 1].size = prev_len + row->size;
        E.row[E.cy-1].chars[E.row[E.cy-1].size]='\0';

        editorUpdateRow(&E.row[E.cy-1]);

        editorDelRow(E.cy);

        E.cy--;
        E.cx = prev_len;
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
        
        case 127: //Backspace
        case CTRL_KEY('h'): //? What does this do?
            editorDelChar();
            break;

        case CTRL_KEY('s'):
            editorSave();
            break;

        case CTRL_KEY('f'):
            editorFind();
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
    //? I don't get the logic here
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
void editorDrawRows(struct abuf *ab)
{
    for (int y = 0; y < E.screenrows; y++) {
        int filerow = y + E.rowoff;

        if (filerow >= E.numrows) {
            abAppend(ab, "~", 1);
        } else {
            int len = E.row[filerow].rsize - E.coloff;
            if (len < 0) len = 0;
            if (len > E.screencols) len = E.screencols;

            abAppend(ab, "\x1b[K", 3); // clear line
            abAppend(ab,
                &E.row[filerow].render[E.coloff],
                len);
        }

        abAppend(ab, "\r\n", 2);
    }
}

void editorDrawStatusBar(struct abuf *ab)
{
    abAppend(ab, "\x1b[7m", 4);

    char status[80];
    int len = snprintf(status, sizeof(status),
        "%.20s - %d lines",
        E.filename ? E.filename : "[No Name]",
        E.numrows);

    if (len > E.screencols) len = E.screencols;

    abAppend(ab, status, len);

    while (len < E.screencols) {
        abAppend(ab, " ", 1);
        len++;
    }

    abAppend(ab, "\x1b[m", 3);
    abAppend(ab, "\r\n", 2);

    int msglen = strlen(E.statusmsg);
    if (msglen > E.screencols) msglen = E.screencols;

    abAppend(ab, E.statusmsg, msglen);
}

void editorRefreshScreen()
{
    editorScroll();

    struct abuf ab = {NULL, 0};

    abAppend(&ab, "\x1b[2J", 4); // clear screen
    abAppend(&ab, "\x1b[H", 3);  // move cursor top

    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);

    // position cursor
    char buf[32];
    int cx = (E.rx - E.coloff) + 1;
    int cy = (E.cy - E.rowoff) + 1;

    int len = snprintf(buf, sizeof(buf), "\x1b[%d;%dH", cy, cx);
    abAppend(&ab, buf, len);

    // Write to back buffer
    WriteConsoleA(E.hBackBuffer, ab.b, ab.len, NULL, NULL);
    
    // Swap buffers - this atomically switches displayed content
    SetConsoleActiveScreenBuffer(E.hBackBuffer);
    
    // Swap handles for next frame
    HANDLE temp = E.hBackBuffer;
    E.hBackBuffer = E.hFrontBuffer;
    E.hFrontBuffer = temp;

    abFree(&ab);
}

/*** Searching ***/
char *editorPrompt(char *prompt)
{
    size_t bufsize = 128;
    char *buf = malloc(bufsize);

    size_t buflen = 0;
    buf[0] = '\0';

    while (1)
    {
        snprintf(E.statusmsg,sizeof(E.statusmsg),prompt,buf);
        editorRefreshScreen();

        int c = editorReadKey(); //? What's it with all these defining objects in loops

        if (c == '\r')
        {
            if (buflen != 0)
            {
                E.statusmsg[0] = '\0';
                return buf;
            }
            
        } else if (c == 127 || c == CTRL_KEY('h')) //We press backspace
        {
            if (buflen != 0)
            {
                buflen--;
                buf[buflen] = '\0';
            }
            
        } else if (!iscntrl(c) && c<128)
        {
            if (buflen == bufsize - 1)
            {
                bufsize *= 2;
                buf = realloc(buf,bufsize);
            }

            buf[buflen++] = c;
            buf[buflen] = '\0';
            
        }
        
    }
    
}

void editorFind()
{
    char *query = editorPrompt("Search: %s");

    if (query == NULL) return;

    for (int i = 0; i < E.numrows; i++)
    {
        char *match = strstr(E.row[i].chars,query);

        if(match)
        {
            E.cy = i;
            E.cx = match - E.row[i].chars;
            E.rowoff = E.numrows; //force scroll
            break;
        }
    }
    
    
}
/*** saving ***/

char *editorRowsToString(int *buflen)
{
    int totlen=0;

    for (int j = 0; j<E.numrows;j++)
        totlen += E.row[j].size + 1; //+1 for '\n

    *buflen = totlen;

    char *buf = malloc(totlen);
    char*p = buf;

    for (int j = 0; j<E.numrows;j++){
        memcpy(p,E.row[j].chars,E.row[j].size);
        p += E.row[j].size;

        *p = '\n';
        p++;
    }
    return buf;
}

void editorSave()
{
    if (E.filename == NULL) return;

    int len;
    char *buf = editorRowsToString(&len);

    FILE *fp = fopen(E.filename,"w");
    if(!fp){
        free(buf);
        return;
    }

    fwrite(buf,1,len,fp);
    fclose(fp);

    free(buf);

    snprintf(E.statusmsg,sizeof(E.statusmsg), "%d bytes written to disk", len);
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

    if (getWindowSize(&E.screenrows, &E.screencols) == -1) { //? Why would this be -1?
        exit(1);
    }

    E.screenrows -= 2; //? why
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
        // So we never break out of the loop rather the program ends from the functions themselves?
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}