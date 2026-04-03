#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <time.h>

#define CTRL_KEY(k) ((k) & 0x1f)
#define KILO_TAB_STOP 8 //We're moving towards stops, the same way you'd do it in obsidian
#define HL_NORMAL 0
#define HL_MATCH 1
#define HL_OTHER 2

void editorInsertNewline();
void editorInsertChar(int c);
void editorSave();
void editorFind();
void editorScroll();
void TypewriterScroll();

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
    int *hl;
    
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
    WORD default_attr;
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

/*** rows ***/

void editorUpdateRow(erow *row) /// Prepares the render version of a row
{
    int tabs = 0;
    for (int j = 0; j < row->size; j++)
        if (row->chars[j] == '\t') tabs++;

    free(row->render); 
    row->render = malloc(row->size + tabs * (KILO_TAB_STOP - 1) + 1);

    int idx = 0;
    for (int j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') {
            row->render[idx++] = ' '; //Every tab is atleast 1 space
            while (idx % KILO_TAB_STOP != 0)
                row->render[idx++] = ' ';
        } else {
            row->render[idx++] = row->chars[j];
        }
    }

    row->render[idx] = '\0';
    row->rsize = idx;
    row->hl = realloc(row->hl, sizeof(int) * row->rsize);
    memset(row->hl, 0, sizeof(int) * row->rsize);
}

void editorAppendRow(char *s, size_t len)
{
    ///Appends characters as a row to the row array
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

    int at = E.numrows; //The last row in our array of Erows
    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1); /*Allocate enough space for both
    the characters and the end of line character*/

    memcpy(E.row[at].chars, s, len); //Copy the characters from s to our new row's address
    E.row[at].chars[len] = '\0';

    E.row[at].rsize = 0;
    E.row[at].render = NULL; //Setup its renderer
    E.row[at].hl = NULL;

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
    free(E.row[at].hl);

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
    //E.statusmsg_time = time(NULL);
}

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
        E.row[E.cy].hl = NULL;

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
        E.row[cy + 1].hl = NULL;

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
            if (len < 0) len = 0; //If it's shorter than the row width then give us 0(if the row contents are off screen to the left)
            if (len > E.screencols) len = E.screencols; //If it's longer than the row then only display the part that is the screen width

            char *c = &E.row[filerow].render[E.coloff]; //for row filerow get me the location of the first character at the column offset
            int *hl = &E.row[filerow].hl[E.coloff]; //From the color array get me the first character as well

            int last_hl = -1;
            for (int j = 0; j < len; j++)
            {
                if (hl[j] != last_hl) {
                    WORD attr = E.default_attr;
                    if (hl[j] == HL_MATCH) {
                        // Bright (current match)
                        attr = BACKGROUND_RED | BACKGROUND_GREEN; // yellow
                    }
                    else if (hl[j] == HL_OTHER) {
                        // Dim (other matches)
                        attr = BACKGROUND_BLUE | FOREGROUND_INTENSITY; 
                    }
                    SetConsoleTextAttribute(E.hOutput, attr);
                    last_hl = hl[j];
                }
                WriteConsoleA(E.hOutput, &c[j], 1, NULL, NULL);
            }
            SetConsoleTextAttribute(E.hOutput, E.default_attr);
        
            WriteConsoleA(E.hOutput, "\r\n", 2, NULL, NULL);
        }
    }
}

void editorDrawStatusBar() {
    WORD inverse_attr = ((E.default_attr & 0x0F) << 4) | ((E.default_attr & 0xF0) >> 4);
    SetConsoleTextAttribute(E.hOutput, inverse_attr);

    char status[80];
    int len = sprintf(status, "%.20s - %d lines",
        E.filename ? E.filename : "[No Name]",
        E.numrows);

    if (len > E.screencols) len = E.screencols;
    WriteConsoleA(E.hOutput, status, len, NULL, NULL);

    char spaces[E.screencols - len];
    memset(spaces, ' ', E.screencols - len);
    WriteConsoleA(E.hOutput, spaces, E.screencols - len, NULL, NULL);

    WriteConsoleA(E.hOutput, "\r\n", 2, NULL, NULL);

    SetConsoleTextAttribute(E.hOutput, E.default_attr);

    int msglen = E.statusmsg[0] ? strlen(E.statusmsg) : 0;
    if (msglen > E.screencols) msglen = E.screencols;
    WriteConsoleA(E.hOutput, E.statusmsg, msglen, NULL, NULL);
}

void editorRefreshScreen()
{
    TypewriterScroll();

    // Clear the screen using Windows API
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(E.hOutput, &csbi)) {
        DWORD written;
        COORD coord = {0, 0};
        FillConsoleOutputCharacter(E.hOutput, ' ', csbi.dwSize.X * csbi.dwSize.Y, coord, &written);
        FillConsoleOutputAttribute(E.hOutput, csbi.wAttributes, csbi.dwSize.X * csbi.dwSize.Y, coord, &written);
        SetConsoleCursorPosition(E.hOutput, coord);
    }

    editorDrawRows();
    editorDrawStatusBar();

    COORD cursor_coord = {(SHORT)(E.rx - E.coloff), (SHORT)(E.cy - E.rowoff)};
    SetConsoleCursorPosition(E.hOutput, cursor_coord);
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

    if (E.cy >= E.rowoff + E.screenrows) //If the cursor position is below the screen now
        E.rowoff = E.cy - E.screenrows + 1; //To get the new top of screen row. So basically we're inching the screen down, but it works even if we somehow jump

    if (E.rx < E.coloff)
        E.coloff = E.rx;

    if (E.rx >= E.coloff + E.screencols)
        E.coloff = E.rx - E.screencols + 1;
}

void TypewriterScroll()
{
    int halfway = E.screenrows/2;

    E.rowoff = E.cy - halfway;
    E.rowoff = E.rowoff < 0 ? 0 : E.rowoff;

    //! Horizontal cursor movement is the same
    if (E.cy < E.numrows) {
        E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
    } else {
        E.rx = E.cx;
    }

    if (E.rx < E.coloff)
        E.coloff = E.rx;

    if (E.rx >= E.coloff + E.screencols)
        E.coloff = E.rx - E.screencols + 1;
}

char *editorPrompt(char *prompt, void (*callback)(char *, int))
{
    size_t bufsize = 128;
    char *buf = malloc(bufsize);

    size_t buflen = 0;
    buf[0] = '\0';

    while (1)
    {
        snprintf(E.statusmsg,sizeof(E.statusmsg), prompt,buf);
        editorRefreshScreen();

        int c = editorReadKey();
        if (c == '\r')
        {
            if (buflen != 0)
            {
                E.statusmsg[0] = '\0';
                if (callback) callback(buf,c);
                return buf;
            }
        } else if (c==27) //ESC
        {
            E.statusmsg[0] = '\0';
            if (callback) callback(buf,c);
            free(buf);
            return NULL;
        }
        else if (c==127 || c==CTRL_KEY('h'))
        {
            if (buflen != 0)
            {
                buflen--;
                buf[buflen]= '\0';
            }
        }
        else if (!iscntrl(c) && c<128)
        {
            if (buflen == bufsize -1)
            {
                bufsize *=2;
                buf = realloc(buf,bufsize);
            }
            
            buf[buflen++] = c;
            buf[buflen] = '\0';
        }

        if (callback) callback(buf,c);

    }
}

void editorFindCallback(char *query, int key)
{
    static int last_match = -1;
    static int direction = 1;

    // Clear all highlights
    for (int i = 0; i < E.numrows; i++) {
        memset(E.row[i].hl, HL_NORMAL, sizeof(int) * E.row[i].rsize);
    }

    if (key == '\r' || key == 27) {
        last_match = -1;
        direction = 1;
        return;
    } else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
        direction = 1;
    } else if (key == ARROW_LEFT || key == ARROW_UP) {
        direction = -1;
    } else {
        last_match = -1;
        direction = 1;
    }

    int query_len = strlen(query);

    // 🔥 STEP 1: Highlight ALL matches as HL_OTHER
    for (int i = 0; i < E.numrows; i++) {
        char *row = E.row[i].chars;
        char *match = row;

        while ((match = strstr(match, query)) != NULL) {
            int cx = match - row;

            int rx = editorRowCxToRx(&E.row[i], cx);

            for (int j = 0; j < query_len; j++) {
                if (rx + j < E.row[i].rsize)
                    E.row[i].hl[rx + j] = HL_OTHER;
            }

            match += query_len;
        }
    }

    // 🔥 STEP 2: Find NEXT match (your existing logic)
    int current = last_match;

    for (int i = 0; i < E.numrows; i++) {
        current += direction;

        if (current == -1) current = E.numrows - 1;
        else if (current == E.numrows) current = 0;

        char *match = strstr(E.row[current].chars, query);

        if (match) {
            last_match = current;

            E.cy = current;
            E.cx = match - E.row[current].chars;
            E.rx = editorRowCxToRx(&E.row[current], E.cx);

            // Center scroll
            E.rowoff = E.cy - E.screenrows / 2;
            if (E.rowoff < 0) E.rowoff = 0;

            E.coloff = E.rx - E.screencols / 2;
            if (E.coloff < 0) E.coloff = 0;

            // 🔥 STEP 3: Override current match → HL_MATCH
            int rx = E.rx;
            for (int j = 0; j < query_len; j++) {
                if (rx + j < E.row[current].rsize)
                    E.row[current].hl[rx + j] = HL_MATCH;
            }

            break;
        }
    }
}

void editorFind()
{
    char *query = editorPrompt("Search: %s",editorFindCallback);

    if (query)
    {
        free(query);
    }
}


/*** File I/O ***/
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
    if (E.filename == NULL)
    {
        E.filename = editorPrompt("Save as: %s",NULL);

        if (E.filename == NULL)
        {
            snprintf(E.statusmsg,sizeof(E.statusmsg),"Save aborted");
            return;
        }
    }

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

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(E.hOutput, &csbi);
    E.default_attr = csbi.wAttributes;

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