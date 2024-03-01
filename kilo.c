/*** includes ***/

#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <sys/ioctl.h>


/*** defines ***/

#define CTRL_KEY(k) ((k) & 0x1f)


/*** data ***/

struct editorConfig{
    int screenrows;
    int screencols;
    struct termios orig_termios;
};

struct editorConfig E;


/*** terminal ***/

void die(const char *s){

    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}

// Disabling raw mode at exit
void disableRawMode(){
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) // TCSAFLUSH discards any unread input before applying the changes to the terminal
        die("tcsetattr"); // Call die when they fail
}

void enableRawMode(){
    if(tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr"); // Call die when they fail
    atexit(disableRawMode); // Called automatically when program exits

    struct termios raw = E.orig_termios;
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

//Wait for a keypress and return it.
char editorReadKey(){
    int nread;
    char c;
    while((nread = read(STDIN_FILENO, &c, 1)) != 1){
        if(nread == -1 && errno != EAGAIN) die("read");
    }
    return c;
}

int getWindowSize(int *rows, int *cols){
    struct winsize ws;

    // ioctl will place num of number of cols wide and num of tows high into the terminal is into the given winsize struct
    if (ioctl(STDERR_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0){
        return -1;
    } else{
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/*** output ***/

//Handles drawing rows of the buffer of text being edited.
void editorDrawRows(){
    int y;
    for (y = 0; y < E.screenrows; y++){
        write(STDOUT_FILENO, "~\r\n", 3);
    }
}

void editorRefreshScreen(){
    write(STDOUT_FILENO, "\x1b[2J", 4); // 4 means we're writing 4 bytes to terminal
    //First byte \x1b (escape char) or 27 in dec
    //<esc>[2J command left cursor at bottom of screen

    write(STDOUT_FILENO, "\x1b[H", 3); // H command to position cursor

    editorDrawRows();

    write(STDOUT_FILENO, "\x1b[H", 3); // Escape sequence to reposition cursor back up to top-left corner
}


/*** input ***/

// Waits for a keypress and handles it.
void editorProcessKeypress(){
    char c = editorReadKey();

    switch (c){
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
    }
}


/*** init ***/

//Initialize  all the fields into the E struct
void initEditor(){
    if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main(){

    //Turning off echoing
    enableRawMode();

    // Read 1 byte from std input until there are no more bytes to read
    // Exit if user types q
    while(1){
        editorProcessKeypress();
    }

    return 0;
}