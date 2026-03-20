#include <windows.h>
#include <stdio.h> //What does this library do?
#include <stdlib.h>
#include <ctype.h>

#define CTRL_KEY(k) ((k) & 0x1f)

struct editorConfig{
    int cx, cy; //Cursor positions
    int screenrows;
    int screencols;

    HANDLE hInput; 
    HANDLE hOutput;

    DWORD originalMode;
};

struct editorConfig E; //? Why is there a struct in the definition of this?

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
    for (int y = 0; y < E.screenrows; y++)
    {
        printf("~\r\n");
    }
}

void editorRefreshScreen()
{
    editorClearScreen();
    editorDrawRows();
    printf("\x1b[H");
}

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

    if (getWindowSize(&E.screenrows, &E.screencols)==-1) //? How does get window size work?
    {
        exit(1);
    }
}

int main()
{
    enableRawMode();
    initEditor();

    while (1)
    {
        editorRefreshScreen();
        editorProcessKeypress();
    }
    
    return 0;
}