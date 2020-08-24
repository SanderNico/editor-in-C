/*** includes ***/
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>

/*** data ***/
struct termios orig_termios;

/*** defines ***/
#define CTRL_KEY(k) ((k) & 0x1f)

/*** terminal ***/
//Prints an error message and exits the program
void die(const char *s){
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}

//Disables raw mode at exit
void raw_mode_disable(){
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
        die("tcsetattr");
}

void raw_mode_enable(){
    if(tcgetattr(STDIN_FILENO, &orig_termios) == -1)
        die("tcgetattr");
   
    atexit(raw_mode_disable);

    struct termios raw = orig_termios;
    /*
     * ICRNL: Reads 'Ctrl-M' as a 13 instead of a 10
     * IXON: Disables 'Ctrl-S' and 'Ctrl-Q'
     * BRKINT: When turned on, a break condition will cause a SIGINT
     * INPCK: Enabls parity checking
     * ISTRIP: Causes the 8th bit of each input to be strip (set it to 0)
     */ 
    raw.c_iflag &= ~(ICRNL | IXON | BRKINT | INPCK | ISTRIP);

    /*
     * OPOST: Turns of output processing
     */
    raw.c_oflag &= ~(OPOST);

    /*
     *
     */
    raw.c_cflag |= (CS8); 

    /*
     * ECHO: Causes each key to be printed to the terminal
     * ISIG: Turns off 'Ctrl-C' and 'Ctrl-Z' signals
     * ICANON: Turns of canonical mode and lets us read input byte-by-byte
     * IEXTEN: Disables 'Ctrl-V'
     */
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    /*
     * VMIN: Sets the minimum number of bytes of input needed before read() can return
     * VTIME: Sets the amount of time to wait before read() returns
     */
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die("tcsetattr");
}

char editor_read_key(){
    int nread;
    char c;
    while((nread = read(STDIN_FILENO, &c, 1)) != 1){
        if(nread == -1 && errno != EAGAIN)
            die("read");
    }
    return c;
}

/*** output ***/
void editor_draw_rows() {
    int y;
    for(y = 0; y < 24; y++){
        write(STDOUT_FILENO, "\r\n", 3);
    }
}

void editor_refresh_screen(){
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    editor_draw_rows();

    write(STDOUT_FILENO, "\x1b[H", 3);
}

/*** input ***/
void editor_process_keypress() {
    char c = editor_read_key();

    switch(c) {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
    }
}


/*** init ***/
int main(){
    raw_mode_enable(); //Enter raw mode

    while (1){
        editor_refresh_screen();
        editor_process_keypress();
    }

    return 0;
}
