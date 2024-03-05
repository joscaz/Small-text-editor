/*** includes ***/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <string.h>


/*** defines ***/

#define KILO_VERSION "0.0.1"

#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};


/*** data ***/

typedef struct erow {
    int size;
    char *chars;
} erow;

struct editorConfig{
    int cx, cy; // Cx = horiz coord of cursor, Cy = vertic coord (row)
    int screenrows;
    int screencols;
    int numrows;
    erow row;
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
int editorReadKey(){
    int nread;
    char c;
    while((nread = read(STDIN_FILENO, &c, 1)) != 1){
        if(nread == -1 && errno != EAGAIN) die("read");
    }

    if (c == '\x1b'){
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '['){
            if (seq[1] >= '0' && seq[1] <= '9'){
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if (seq[2] == '~'){
                    switch (seq[1]){
                        case '1': return HOME_KEY;
                        case '3': return DEL_KEY;
                        case '4': return END_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                }
            } else {
                switch(seq[1]){
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                }
            }
        } else if (seq[0] == 'O'){
            switch (seq[1]) {
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }
        return '\x1b';
    } else {
        return c;
    }
}

int getCursorPosition(int *rows, int *cols){

    char buf[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

    while (i < sizeof(buf) - 1){
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0'; // printf() excepts strings to end with a 0 byte

    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

    return 0;
}

int getWindowSize(int *rows, int *cols){
    struct winsize ws;

    // ioctl will place num of number of cols wide and num of tows high into the terminal is into the given winsize struct
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0){
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1; // move the cursor to the bottom-right corner
        return getCursorPosition(rows, cols);
    } else{
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}


/***file i/o  ***/

void editorOpen(char *filename){
    FILE *fp = fopen(filename, "r");
    if (!fp) die("fopen");

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    linelen = getline(&line, &linecap, fp);
    if (linelen  != -1) {
        while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) linelen--;
        E.row.size = linelen;
        E.row.chars = malloc(linelen + 1);
        memcpy(E.row.chars, line, linelen);
        E.row.chars[linelen] = '\0';
        E.numrows = 1;
    }
    free(line);
    fclose(fp);
}


/*** append buffer ***/

// C does not have dynamic strings, so we'll create ours that supports append operation
struct abuf{
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

// Append operation
void abAppend(struct abuf *ab, const char *s, int len){
    char *new = realloc(ab->b, ab->len + len);

    if (new == NULL) return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

// Destructor
void abFree(struct abuf *ab){
    free(ab->b);
}


/*** output ***/

//Handles drawing rows of the buffer of text being edited.
void editorDrawRows(struct abuf *ab){
    int y;
    for (y = 0; y < E.screenrows; y++){
        if (y >= E.numrows) {
            if (E.numrows == 0 && y == E.screenrows / 3) {
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome), "Kilo editor -- version %s", KILO_VERSION);
                if (welcomelen > E.screencols) welcomelen = E.screencols;
                int padding = (E.screencols - welcomelen) / 2;
                if (padding) {
                    abAppend(ab, "~", 1);
                    padding--;
                }
                while (padding--) abAppend(ab, " ", 1);
                abAppend(ab, welcome, welcomelen);
            } else {
                abAppend(ab, "~", 1);
            }
        } else {
            int len = E.row.size;
            if (len > E.screencols) len = E.screencols;
            abAppend(ab, E.row.chars, len);
        }

        abAppend(ab, "\x1b[K", 3);
        if (y < E.screenrows - 1){
            abAppend(ab, "\r\n", 2);
        }
    }
}

void editorRefreshScreen(){
    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6);
    abAppend(&ab, "\x1b[H", 3); // H command to position cursor

    editorDrawRows(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[H", 3); // Escape sequence to reposition cursor back up to top-left corner
    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}


/*** input ***/

void editorMoveCursor(int key){
    switch (key){
        case ARROW_LEFT:
            if (E.cx != 0) E.cx--;
            break;
        case ARROW_RIGHT:
            if (E.cx != E.screencols - 1) E.cx++;
            break;
        case ARROW_UP:
            if (E.cy != 0) E.cy--;
            break;
        case ARROW_DOWN:
            if (E.cy != E.screenrows - 1) E.cy++;
            break;
    }
}

// Waits for a keypress and handles it.
void editorProcessKeypress(){
    int c = editorReadKey();

    switch (c){
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;

        case HOME_KEY:
            E.cx = 0;
            break;

        case END_KEY:
            E.cx = E.screencols - 1;
            break;
        
        case PAGE_UP:
        case PAGE_DOWN:
            {
                int times = E.screenrows;
                while (times--)
                    editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
            }
            break;
        
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;
    }
}


/*** init ***/

//Initialize  all the fields into the E struct
void initEditor(){
    E.cx = 0;
    E.cy = 0;
    E.numrows = 0;

    if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main(int argc, char *argv[]){

    //Turning off echoing
    enableRawMode();
    initEditor();

    if (argc >= 2){
        editorOpen(argv[1]);
    }
    // Exit if user types q
    while (1){
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}