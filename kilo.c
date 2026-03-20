#include <windows.h>
#include <stdio.h> //What does this library do?

DWORD originalMode;
HANDLE hInput;

void disableRawMode()
{
    SetConsoleMode(hInput,originalMode);
}

void enableRawMode()
{
    hInput = GetStdHandle(STD_INPUT_HANDLE);
    

    GetConsoleMode(hInput, &originalMode);
    atexit(disableRawMode);

    DWORD raw = originalMode;

    raw &= ~(ENABLE_ECHO_INPUT |
             ENABLE_LINE_INPUT |
             ENABLE_PROCESSED_INPUT); 

    SetConsoleMode(hInput,raw);
}

int main()
{
    enableRawMode();

    char c;
    DWORD read;

    HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);

    while (ReadConsoleA(hInput,&c,1,&read,NULL)&&c!='q') //? What is this syntax?
    {
        printf("%d ('%c')\n",c,c);
    }
    
    return 0;
}