/*** includes ***/

#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>


/*** data ***/

struct termios orig_termios;

/*** terminal ***/

void die(const char *s){
    perror(s);
    exit(1);
}

// Disabling raw mode at exit
void disableRawMode(){
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1) // TCSAFLUSH discards any unread input before applying the changes to the terminal
        die("tcsetattr"); // Call die when they fail
}

void enableRawMode(){
    if(tcgetattr(STDIN_FILENO, &orig_termios) == -1) die("tcgetattr"); // Call die when they fail
    atexit(disableRawMode); // Called automatically when program exits

    struct termios raw = orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    // BRKINT - when turned on, a breadk condition will cause SIGINT signal, to be sent to program, like pressing Ctrl-C
    // INPCK - enables parity checking (doesn't seem to apply to modern terminal emulators)
    // ISTRIP - causes 8th bit of each input byte to be stripped, set to 0 (prob already turned on)
    // IXON - disabling Ctrl-S and Ctrl-Q signals
    // ICRNL - Ctrl-M is now read as a 13 (carriage return), as well as the enter key

    raw.c_oflag &= ~(OPOST);// Turning off all output processing

    raw.c_cflag |= (CS8); // Bitmask with multiple bits (not a flag). It sets the character size (CS) to 8 bits per byte.

    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    // ICANON - turning off canonical mode, meaning it will be reading input byte-by-byte instead of line-by-line
    // ISIG - turning off Ctrl-C and Ctrl-Z signals and now can be read as a byte
    // IEXTEN - disabling Ctrl-V and now can be read as a byte

    // Sets min number of bytes if input needed before read() can return
    // Set to 0 so that read() returns as soon as there is any input to be read.
    raw.c_cc[VMIN] = 0;

    // Sets the max amount of time to wait before read() returns. In tenths of a second.
    // Set to 1/10 of a second, or 100 milliseconds
    raw.c_cc[VTIME] = 1; //

    if (tcsetattr(STDERR_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}


/*** init ***/
int main(){

    //Turning off echoing
    enableRawMode();

    // Read 1 byte from std input until there are no more bytes to read
    // Exit if user types q
    while(1){
        char c = '\0';
        if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) die("read");
        if (iscntrl(c)){ // iscntrl tests whether char is a control character (ASCII codes 0-31, 127 as well)
            printf("%d\r\n", c);
        } else{
            printf("%d ('%c')\r\n", c, c);
        }
        if(c == 'q') break;
    }

    return 0;
}