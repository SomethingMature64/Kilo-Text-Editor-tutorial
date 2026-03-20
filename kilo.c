#include <windows.h>
#include <stdio.h> //What does this library do?
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <time.h>

#define CTRL_KEY(k) ((k) & 0x1f)
#define KILO_TAB_STOP 8 

typedef struct erow {
    int size;
    char *chars;

    int rsize;
    char *render; //?
} erow;
struct editorConfig{
    int cx, cy; //Cursor positions
    int rx; //?

    //We use both of these for scrolling
    int rowoff;
    int coloff;

    int screenrows;
    int screencols;

    int numrows;
    erow *row;

    char *filename; //? Why does this work 

    char statusmsg[80];
    time_t statusmsg_time;

    HANDLE hInput; 
    HANDLE hOutput;

    DWORD originalMode;
};
struct editorConfig E;

int getWindowSize(int *rows, int *cols);
void editorAppendRow(char *s, size_t len);
void editorUpdateRow(erow *row);
void editorDrawStatusBar();

int getWindowSize(int *rows, int *cols)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    
    if (!GetConsoleScreenBufferInfo(E.hOutput,&csbi))
    {
        return -1;
    }
    
    *cols = csbi.srWindow.Right - csbi.srWindow.Left +1;
    *rows = csbi.srWindow.Bottom - csbi.srWindow.Top +1;

    return 0;
}

void initEditor()
{
    E.cx = 0;
    E.cy = 0;

    E.numrows = 0;
    E.row = NULL;

    if (getWindowSize(&E.screenrows, &E.screencols)==-1) //? How does get window size work?
    {
        exit(1);
    }
}

void editorOpen(const char *filename)
{
    FILE *fp = fopen(filename,"r");
    if (!fp) return;

    E.filename = _strdup(filename);

    char line[1024]; //todo need to look into strings in c again

    while (fgets(line,sizeof(line),fp))
    {
        int len = strlen(line);

        while (len>0 && (line[len-1]=='\n' || line[len-1]=='\r'))
            len--;

        editorAppendRow(line,len);
    }

    fclose(fp);
}

void editorAppendRow(char*s, size_t len)
{
    E.row = realloc(E.row,sizeof(erow) * (E.numrows+1));

    int at = E.numrows;
    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);

    memcpy(E.row[at].chars,s,len);
    E.row[at].chars[len] = '\0';

    E.row[at].rsize = 0;
    E.row[at].render = NULL;

    editorUpdateRow(&E.row[at]);

    E.numrows++;
}

void editorUpdateRow(erow *row)
{
    int tabs = 0;
    for(int j = 0; j<row->size;j++)
        if (row->chars[j] == '\t') tabs++;

    free(row->render);
    row->render = malloc(row->size + tabs*(KILO_TAB_STOP-1)+1);

    int idx = 0;
    for(int j = 0; j < row->size; j++)
    {
        if (row->chars[j]=='\t'){
            row->render[idx++] = ' ';
            while (idx % KILO_TAB_STOP != 0)
            {
                row -> render[idx++] = ' ';
            }
            
        }
        else{
            row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->rsize = idx;
}

void editorScroll()
{
    E.rx = E.cx;
    if (E.cy < E.rowoff)
        E.rowoff = E.cy;
    
    if (E.cy >= E.rowoff + E.screenrows)
        E.rowoff = E.cy - E.screenrows + 1;

    if (E.cx < E.coloff)
        E.coloff = E.cx;

    if (E.cx >= E.coloff + E.screencols)
        E.coloff = E.cx - E.screencols + 1;
}

void disableRawMode()
{
    SetConsoleMode(E.hInput,E.originalMode); //Why did we switch to a struct?
}

void enableRawMode()
{
    E.hInput = GetStdHandle(STD_INPUT_HANDLE);
    E.hOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    

    GetConsoleMode(E.hInput, &E.originalMode); 
    /*
    ! Here we store the bit flags that represent our current console's
    ! settings in our DWORD
    */

    DWORD raw = E.originalMode;

    raw &= ~(ENABLE_ECHO_INPUT |
             ENABLE_LINE_INPUT |
             ENABLE_PROCESSED_INPUT); //? What do each of these do?

    SetConsoleMode(E.hInput,raw); //! Change the console mode of the standard output to be of the raw settings
    
    DWORD mode;
    GetConsoleMode(E.hOutput, &mode);
    SetConsoleMode(E.hOutput, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    
    atexit(disableRawMode); //Does the order of this function's placement matter?
}

int editorReadKey(){
    INPUT_RECORD record; //? What's this
    DWORD read;

    while(1)
    {   //? What is going on here?
        ReadConsoleInput(E.hInput,&record,1,&read);

        if (record.EventType == KEY_EVENT &&
            record.Event.KeyEvent.bKeyDown)
        {
            return record.Event.KeyEvent.uChar.AsciiChar; 
        }
        
    }
}

void editorProcessKeypress()
{
    int c = editorReadKey();
    if (c==CTRL_KEY('q'))
    {
        exit(0);
    }

    if (iscntrl(c))
    {
        printf("%d\n",c);
    } else
    {
        printf("%d ('%c')\n",c,c);
    }
}

void editorClearScreen()
{
    printf("\x1b[2J"); //Gotta be putting explanations for my ascii chars here
    printf("\x1b[H");
}

void editorDrawRows()
{
    for (int i = 0; i < E.screenrows; i++)
    {
        int filerow = i + E.rowoff; //? Define outside the loop?
        if (filerow >= E.numrows)
            WriteConsole(E.hOutput,"~\r\n",3,NULL,NULL);
        else
        {
            int len = E.row[filerow].rsize - E.coloff;
            if (len < 0) len = 0;
            if (len > E.screencols) len = E.screencols;

            WriteConsole(E.hOutput,&E.row[filerow].render[E.coloff],len,NULL,NULL);

            WriteConsoleA(E.hOutput,"\r\n", 2, NULL,NULL);
        }
    }
}

void editorRefreshScreen()
{
    editorScroll();
    editorClearScreen();
    editorDrawRows();
    editorDrawStatusBar();
    printf("\x1b[H");
}



void editorDrawStatusBar() {
    printf("\x1b[7m"); // inverted colors

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
}

int main(int argc, char *argv[])
{
    enableRawMode();
    initEditor();

    if (argc >= 2)
    {
        editorOpen(argv[1]);
    }

    while (1)
    {
        editorRefreshScreen();
        editorProcessKeypress();
    }
    
    return 0;
}