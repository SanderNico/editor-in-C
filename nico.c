/*** includes ***/
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <string.h>
#include <time.h>

/*** defines ***/
#define NICO_VERSION "0.0.1"
#define NICO_TAB_STOP 8
#define NICO_QUIT_TIMES 3
#define CTRL_KEY(k) ((k) & 0x1f)

enum editor_key {
    BACKSPACE = 127,
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
    int rsize;
    char *chars;
    char *render;
} erow;

struct editorConfig {
    int cx, cy;
    int rx;
    int rowoff;
    int coloff;
    int screenrows;
    int screencols;
    int numrows;
    erow *row;
    int dirty;
    char *filename;
    char statusmsg[80];
    time_t statusmsg_time;
    struct termios orig_termios;
};

struct editorConfig E;

/*** prototypes ***/
void editor_set_status_msg(const char *fmt, ...);

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
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr");
}

void raw_mode_enable(){
    if(tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
        die("tcgetattr");

    atexit(raw_mode_disable);

    struct termios raw = E.orig_termios;
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

int editor_read_key(){
    int nread;
    char c;
    while((nread = read(STDIN_FILENO, &c, 1)) != 1){
        if(nread == -1 && errno != EAGAIN)
            die("read");
    }

    if(c == '\x1b'){
        char seq[3];

        if(read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if(read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if(seq[0] == '['){
            if(seq[1] >= '0' && seq[1] <= '9'){
                if(read(STDIN_FILENO, &seq[2], 1) != 1)
                    return '\x1b';
                if(seq[2] == '~'){
                    switch(seq[1]){
                        case '1': return HOME_KEY;
                        case '3': return DEL_KEY;
                        case '4': return END_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                }
            }else {
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
            switch(seq[1]){
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }

        return '\x1b';
    } else {
        return c;
    }
}

int get_cursor_position(int *rows, int *cols){
    char buf[32];
    unsigned int i = 0;


    if(write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
        return -1;

    while(i < sizeof(buf) - 1){
        if(read(STDIN_FILENO, &buf[i], 1) != 1)
            break;
        if(buf[i] == 'R')
            break;
    }
    buf[i] = '\0';

    if(buf[0] != '\x1b' || buf[1] != '[')
        return -1;

    if(sscanf(&buf[2], "%d;%d", rows, cols) != 2)
        return -1;

    return 0;
}

int get_window_size(int *rows, int *cols){
    struct winsize ws;

    if(1 || ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if(write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12))
            return -1;
        return get_cursor_position(rows, cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/*** row operations ***/
int editor_row_cx_to_rx(erow *row, int cx){
  int rx = 0;
  int j;
  for (j = 0; j < cx; j++){
    if(row->chars[j] == '\t')
      rx += (NICO_TAB_STOP - 1) - (rx % NICO_TAB_STOP);
    rx++;
  }
  return rx;
}

void editor_update_row(erow *row){
  int tabs = 0;
  int j;

  for(j = 0; j < row->size; j++){
    if(row->chars[j] == '\t')
      tabs++;
  }

  free(row->render);
  row->render = malloc(row->size + tabs * (NICO_TAB_STOP - 1) + 1);

  int idx = 0;
  for(j = 0; j < row->size; j++){
    if(row->chars[j] == '\t'){
      row->render[idx++] = ' ';
      while (idx % NICO_TAB_STOP != 0)
        row->render[idx++] = ' ';
    } else {
      row->render[idx++] = row->chars[j];
    }
  }
  row->render[idx] = '\0';
  row->rsize = idx;
}

void editor_append_row(char *s, size_t len){
  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

  int at = E.numrows;
  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';

  E.row[at].rsize = 0;
  E.row[at].render = NULL;
  editor_update_row(&E.row[at]);

  E.numrows++;
  E.dirty++;
}

void editor_row_insert_char(erow *row, int at, int c){
  if (at < 0 || at > row->size)
    at = row->size;
  row->chars = realloc(row->chars, row->size + 2);
  memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
  row->size++;
  row->chars[at] = c;
  editor_update_row(row);
  E.dirty++;
}

void editor_row_del_char(erow *row, int at){
  if (at < 0 || at >= row->size)
    return;
  memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
  row->size--;
  editor_update_row(row);
  E.dirty++;
}

/*** editor operations ***/
void editor_insert_char(int c){
  if (E.cy == E.numrows){
    editor_append_row("", 0);
  }
  editor_row_insert_char(&E.row[E.cy], E.cx, c);
  E.cx++;
}

void editor_del_char(){
  if (E.cy == E.numrows)
    return;

  erow *row = &E.row[E.cy];
  if(E.cx > 0){
    editor_row_del_char(row, E.cx -1);
    E.cx--;
  }
}


/*** file i/o ***/
char *editor_rows_to_string(int *buflen){
  int totlen = 0;
  int j;
  for (j = 0; j < E.numrows; j++)
    totlen += E.row[j].size + 1;

  *buflen = totlen;

  char *buf = malloc(totlen);
  char *p = buf;
  for (j = 0; j < E.numrows; j++){
    memcpy(p, E.row[j].chars, E.row[j].size);
    p += E.row[j].size;
    *p = '\n';
    p++;
  }
  return buf;
}

void editor_open(char *filename){
  free(E.filename);
  E.filename = strdup(filename);

  FILE *fp = fopen(filename, "r");
  if(!fp)
    die("fopen");

  char *line = NULL;
  size_t cap = 0;
  ssize_t len;
  while((len = getline(&line, &cap, fp)) != -1) {
    while(len > 0 && ((line[len-1] == '\n') || (line[len-1] == '\r')))
      len--;
    editor_append_row(line, len);
  }
  free(line);
  fclose(fp);
  E.dirty = 0;
}

void editor_save(){
  if(E.filename == NULL)
    return;

  int len;
  char *buf = editor_rows_to_string(&len);

  int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
  if(fd != -1){
    if(ftruncate(fd, len) != -1){
      if(write(fd, buf, len) == len){
        close(fd);
        free(buf);
        E.dirty = 0;
        editor_set_status_msg("%d bytes written to disk", len);
        return;
      }
    }
    close(fd);
  }
  free(buf);
  editor_set_status_msg("Can't save to disk! I/O error: %s", strerror(errno));
}


/*** append buffer ***/
struct abuf {
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

void abuf_append(struct abuf *ab, const char *s, int len){
    char * new = realloc(ab->b, ab->len + len);

    if(new == NULL)
        return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abuf_free(struct abuf *ab){
    free(ab->b);
}

/*** output ***/
void editor_draw_status_bar(struct abuf *ab){
  abuf_append(ab, "\x1b[7m", 4);
  char status[80], rstatus[80];
  int len = snprintf(status, sizeof(status), "%.20s - %d lines",
   E.filename ? E.filename : "[No name]",
   E.numrows, E.dirty ? "(modified)" : "");
  int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", E.cy + 1, E.numrows);
  if(len > E.screencols)
    len = E.screencols;
  abuf_append(ab, status, len);
  while (len < E.screencols){
    if(E.screencols - len == rlen){
      abuf_append(ab, rstatus, rlen);
      break;
    }else{
      abuf_append(ab, " ", 1);
      len++;
    }
  }
  abuf_append(ab, "\x1b[m", 3);
  abuf_append(ab, "\r\n", 2);
}

void editor_draw_message_bar(struct abuf *ab) {
  abuf_append(ab, "\x1b[k", 3);
  int msglen = strlen(E.statusmsg);
  if(msglen > E.screencols)
    msglen = E.screencols;

  if(msglen && time(NULL) - E.statusmsg_time < 5)
    abuf_append(ab, E.statusmsg, msglen);
}

void editor_scroll(){
  E.rx = 0;
  if(E.cy < E.numrows){
    E.rx = editor_row_cx_to_rx(&E.row[E.cy], E.cx);
  }

  if(E.cy < E.rowoff){
    E.rowoff = E.cy;
  }
  if(E.cy >= E.rowoff + E.screenrows){
    E.rowoff = E.cy - E.screenrows + 1;
  }
  if(E.rx < E.coloff){
    E.coloff = E.rx;
  }
  if(E.rx >= E.coloff + E.screencols){
    E.coloff = E.rx - E.screencols + 1;
  }
}

void editor_draw_rows(struct abuf *ab) {
    int y;
    for(y = 0; y < E.screenrows; y++){
      int filerow = y + E.rowoff;
      if(filerow >= E.numrows) {
          if(E.numrows == 0 && y == E.screenrows / 3){
              char welcome[80];
              int welcome_len = snprintf(welcome, sizeof(welcome), "Nico editor -- version %s", NICO_VERSION);
              if(welcome_len > E.screencols) welcome_len = E.screencols;
              int padding = (E.screencols - welcome_len) / 2;
              if(padding){
                  abuf_append(ab, "~", 1);
                  padding--;
              }
              while(padding--)
                  abuf_append(ab, " ", 1);
              abuf_append(ab, welcome, welcome_len);
          }else {
              abuf_append(ab, "~", 1);
          }
        } else {
          int len = E.row[filerow].rsize - E.coloff;
          if(len < 0)
            len = 0;
          if(len > E.screencols)
            len = E.screencols;
          abuf_append(ab, &E.row[filerow].chars[E.coloff], len);
        }

        abuf_append(ab, "\x1b[k", 3);
        abuf_append(ab, "\r\n", 2);

    }

}

void editor_refresh_screen(){
    editor_scroll();

    struct abuf ab = ABUF_INIT;

    abuf_append(&ab, "\x1b[?25l", 6);
    abuf_append(&ab, "\x1b[H", 3);

    editor_draw_rows(&ab);
    editor_draw_status_bar(&ab);
    editor_draw_message_bar(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.rx - E.coloff) + 1);
    abuf_append(&ab, buf, strlen(buf));

    abuf_append(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    abuf_free(&ab);
}

void editor_set_status_msg(const char *fmt, ...){
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time = time(NULL);
}

/*** input ***/
void editor_move_cursor(int key){
    erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

    switch(key){
        case ARROW_LEFT:
            if(E.cx != 0){
              E.cx--;
            } else if (E.cy > 0){
              E.cy--;
              E.cx = E.row[E.cy].size;
            }
            break;
        case ARROW_RIGHT:
            if(row && E.cx < row->size){
              E.cx++;
            } else if (row && E.cx == row->size){
              E.cy++;
              E.cx = 0;
            }
            break;
        case ARROW_UP:
            if(E.cy != 0){
              E.cy--;
            }
            break;
        case ARROW_DOWN:
            if(E.cy != E.numrows){
              E.cy++;
            }
            break;
    }

    row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    int rowlen = row ? row->size : 0;
    if(E.cx > rowlen){
      E.cx = rowlen;
    }
}

void editor_process_keypress() {
    static int quit_times = NICO_QUIT_TIMES

    int c = editor_read_key();

    switch(c) {
        case '\r':
            //TODO
            break;

        case CTRL_KEY('q'):
            if(E.dirty && quit_times > 0){
              editor_set_status_msg("WARNING!! File has unsaved changes. Press Ctrl-Q %d more times to quit", quit_times);
              quit_times--;
              return;
            }
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;

        case CTRL_KEY('s'):
            editor_save();
            break;

        case HOME_KEY:
            E.cx = 0;
            break;

        case END_KEY:
            if(E.cy < E.numrows)
              E.cx = E.row[E.cy].size;
            break;

        case BACKSPACE:
        case CTRL_KEY('h'):
        case DEL_KEY:
            if(c == DEL_KEY)
              editor_move_cursor(ARROW_RIGHT);
            editor_del_char();
            break;

        case PAGE_UP:
        case PAGE_DOWN:
            {
              if(c == PAGE_UP){
                E.cy = E.rowoff;
              }else if (c == PAGE_DOWN){
                E.cy = E.rowoff + E.screenrows - 1;
                if (E.cy > E.numrows)
                  E.cy = E.numrows;
              }
                int times = E.screenrows;
                while(times--)
                    editor_move_cursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
            }
            break;

        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editor_move_cursor(c);
            break;

        case CTRL_KEY('l'):
        case '\x1b':
            break;

        default:
            editor_insert_char(c);
            break;
    }
    quit_times = NICO_QUIT_TIMES;
}


/*** init ***/
void editor_init(){
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.numrows = 0;
    E.row = NULL;
    E.filename = NULL;
    E.dirty = 0;
    E.statusmsg[0] = '\0';
    E.statusmsg_time = 0;

    if(get_window_size(&E.screenrows, &E.screencols) == -1)
        die("get_window_size");

    E.screenrows -= 2;
}

int main(int argc, char *argv[]){
    raw_mode_enable(); //Enter raw mode
    editor_init();
    if(argc >= 2){
      editor_open(argv[1]);
    }

    editor_set_status_msg("HELP:Ctrl-S = save | Ctrl-Q = quit");

    while (1){
        editor_refresh_screen();
        editor_process_keypress();
    }

    return 0;
}
