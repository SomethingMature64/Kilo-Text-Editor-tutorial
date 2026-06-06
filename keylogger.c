// A simple program to read my key presses back to me

#include <windows.h>
#include <stdlib.h>
#include <stdio.h>

#define CTRL_KEY(k) (k & 0x1f)
enum editorkey {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN
};

int ReadChar()
{
    HANDLE cinput = GetStdHandle(STD_INPUT_HANDLE);
    INPUT_RECORD rec;
    DWORD read;

    while (1) {
        ReadConsoleInput(cinput, &rec, 1, &read);

        if (rec.EventType == KEY_EVENT &&
            rec.Event.KeyEvent.bKeyDown)
        {
            KEY_EVENT_RECORD key = rec.Event.KeyEvent;

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

int main ()
{
    int bool = 1;
    while (bool)
    {
        int input = ReadChar();

        if (!iscntrl(input))
        {
            printf("%c\n",input);
        }
        else
        {
            switch (input)
            {
            case CTRL_KEY('q'):
                bool = -1;
                break;
            
            default:
                ;

            }
        }
        
        
    }
    
}