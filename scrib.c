
/******************************* includes *******************************/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE


#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

/****************************** defines ******************************/

#define CTRL_KEY(k) ((k) & 0x1f)

#define SCRIB_VERSION "0.0.1"

//for cursor movement
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





/******************************* data *******************************/

typedef struct erow {
  int size;
  char *chars;
} erow;

//to store the size of terminal
struct editorConfig {

    int cx, cy;  //store cursor position
	int screenrows;
  	int screencols;
    int numrows;
    erow *row;  //array of rows to store each row of text
  	struct termios orig_termios;  //to store original terminal attributes
};
//global struct to store size of terminal
struct editorConfig E;













/******************************* terminal *******************************/

//error handling - errno variable stores the error
void die(const char *s) {

    //clear the screen on exit
	write(STDOUT_FILENO, "\x1b[2J", 4);
  	write(STDOUT_FILENO, "\x1b[H", 3);

  	perror(s);//to print out decriptive error message with tag 's' passed on to the function die
  	exit(1);
}

void disableRawMode() {
	//restore original attributes of terminal on exit from text editor
	//orig_termios was used to store the original 
	//terminal attributes before enetering the program
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
    	die("tcsetattr");

}
void enableRawMode() {

	//get original attributes of terminal
	if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) 
		die("tcgetattr");

	//to call disableRawMode automatically at exit
	atexit(disableRawMode);
	struct termios raw = E.orig_termios; //save the original terminal attributes
	
	//modify input flag c_lflag using & and ~ bit operation
	//IXON to disable ctrl-Q and ctrl-S
	//ICRNL to disable ctrl-M
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);

	//modify output flag c_oflag using & and ~ bit operation
	//OPOST to turn off output processing like "\n" "\r\n"
	raw.c_oflag &= ~(OPOST);

	raw.c_cflag |= (CS8);

	//modify local flag c_lflag using & and ~ bit operation
	//ECHO immediately prints out the character entered in the terminal
	//ICANNON flag turns off canonical mode
	//ISIG to disable ctrl-C, ctrl-Z 
	//IEXTEN  to disable ctrl-V and ctrl-O
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	raw.c_cc[VMIN] = 0;  //min bytes to read before read() returns
  	raw.c_cc[VTIME] = 1; //min time to wait before read() returns

  	//enable raw mode
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) 
		die("tcsetattr");
}

//read a character from terminal and return it
int editorReadKey() {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
      if (nread == -1 && errno != EAGAIN) 
      	die("read");
    }

    //checking for escape sequences
    if (c == '\x1b') {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        //checkingfor arrow keys, pageup/pagedn etc as they begin with [
        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) 
                	return '\x1b';
                if (seq[2] == '~') {
                    //check second char of input seq for page_up, page_down,home,end
                    switch (seq[1]) {
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
            	//check second char for arrow_keys
                switch (seq[1]) {
              	    case 'A': return ARROW_UP;
                	case 'B': return ARROW_DOWN;
                  	case 'C': return ARROW_RIGHT;
                  	case 'D': return ARROW_LEFT;
                  	case 'H': return HOME_KEY;
                  	case 'F': return END_KEY;
                }
            }
        } else if (seq[0] == 'O') {
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


//helper function used in getWindowSize 
//to get the cursor position which is set to bottom left of the terminal 
//and hence deduce thesize of the terminal window
int getCursorPosition(int *rows, int *cols) {
  char buf[32];
  unsigned int i = 0;

  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
    if (buf[i] == 'R') break;
    i++;
  }
  buf[i] = '\0';
  if (buf[0] != '\x1b' || buf[1] != '[') return -1;
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;
  return 0;
}


//get size of terminal, stores the rows and coloumn size of terminal in 'ws'
int getWindowSize(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) 
        	return -1;
      	editorReadKey();
      	return getCursorPosition(rows, cols);
    } else {
      *cols = ws.ws_col;
      *rows = ws.ws_row;
      return 0;
    }
}








/******************************* row operations *****************/

//add the line read from input file into a newly created row 
void editorAppendRow(char *s, size_t len) {

    //increase the size of rows array by 1 to add a new row
    //i.e. reallocate space equal to number of rows read until now
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

    //initialise the new row with text read from file
    int at = E.numrows;
    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';
    E.numrows++;
}









/*********************** file i/o *************************/
void editorOpen(char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) die("fopen");
    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    
    //keep reading until length of row read is 0, i.e. empty row reached end of file
    while ((linelen = getline(&line, &linecap, fp)) != -1) {

        //calculate length of row read
        while (linelen > 0 && (line[linelen - 1] == '\n' ||
                               line[linelen - 1] == '\r'))
            linelen--;
        //add the new row to our existing array of rows
        editorAppendRow(line, linelen);

    }
    free(line);
    fclose(fp);
}













/*********************** append buffer **********************/

//to create our own dynamic string which suports append operation, 
//since does not support dynamic strings
struct abuf {
  char *b;
  int len;
};
#define ABUF_INIT {NULL, 0}


//append operation on string
void abAppend(struct abuf *ab, const char *s, int len) {
    char *new = realloc(ab->b, ab->len + len);
    if (new == NULL) return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}
void abFree(struct abuf *ab) {
    free(ab->b);
}














/******************************** output *************************/

//Write a welcome message at 1/3 of the screen
//draw tildas at the beginning of every row except welcome msg line
//no of rows is obtained by getWindowSize and stored in E.screenrows
void editorDrawRows(struct abuf *ab) {
    int y;

    //go row by row and display each row by appending into ab 
    for (y = 0; y < E.screenrows; y++) {
        
        //drawing a row that does not contain text
        if (y >= E.numrows) {

  		    //if row no = 1/3 of total rows display welcome message
      	    if (E.numrows == 0 && y == E.screenrows / 3) {
      	    	char welcome[80];
      	    	int welcomelen = snprintf(welcome, sizeof(welcome),
      	    	  "SCRIB editor -- version %s", SCRIB_VERSION);
      	    	
      	    	//if terminal window size is not big enough to fit our msg, truncate
      	    	if (welcomelen > E.screencols) 
      	    		welcomelen = E.screencols;
    
      	    	//centering the welcome msg
      	    	int padding = (E.screencols - welcomelen) / 2;
		        if (padding) {
		          abAppend(ab, "~", 1);
		          padding--;
		        }
		        while (padding--) 
		        	abAppend(ab, " ", 1);
    
      	    	abAppend(ab, welcome, welcomelen);
    
    
    	    } 
            else {
      	    	abAppend(ab, "~", 1);//draw tildas
    	    }    
        }
        else {  //drawing a row that contains text
            int len = E.row[y].size;

            //if length of E.row is longer than total coloumns, truncate
            if (len > E.screencols) len = E.screencols;

            //write E.row to text buffer for display
            abAppend(ab, E.row[y].chars, len);
        }
    
    	abAppend(ab, "\x1b[K", 3);
    	if (y < E.screenrows - 1) {
      		abAppend(ab, "\r\n", 2);
    	}
  	}
}

//refresh screen line by line rather than entire screen
void editorRefreshScreen() {
    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6);//hide the cursor
  	//abAppend(&ab, "\x1b[2J", 4); //clear entire screen
  	abAppend(&ab, "\x1b[H", 3);  //reposition cursor to top left corner

  	editorDrawRows(&ab); //draw tildas

  	//move cursor to position stored in cx,cy
  	char buf[32];
  	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
  	abAppend(&ab, buf, strlen(buf));

  	abAppend(&ab, "\x1b[?25h", 6); //show the cursor

  	write(STDOUT_FILENO, ab.b, ab.len);
  	abFree(&ab);
}














/**************************** input *******************************/

//move the cursor with a,d,w,s
void editorMoveCursor(int key) {

	//if conditions prevent cursor from going out of window
	switch (key) {
	  case ARROW_LEFT:
	      if (E.cx != 0) { 
	        E.cx--;
	      }
	      break;
	  case ARROW_RIGHT:
	      if (E.cx != E.screencols - 1) {
	        E.cx++;
	      }
	      break;
	  case ARROW_UP:
	      if (E.cy != 0) {
	        E.cy--;
	      }
	      break;
	  case ARROW_DOWN:
	      if (E.cy != E.screenrows - 1) {
	        E.cy++;
	      }
	      break;
	}
}


//read a key from terminal using editorReadKey() and handle it
void editorProcessKeypress() {
  int c = editorReadKey();

  switch (c) {
    case CTRL_KEY('q'): //quit when ctrl-q is pressed 

    	//clear the screen and exit
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
    	//move cursor to top or bottom of page with page_up and page_down keys
        int times = E.screenrows;
        while (times--)
            editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
    }
    break;

    //for moving cursor
    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
      editorMoveCursor(c);
      break;
  }
}


















/******************************* init *******************************/

void initEditor() {

	//initialise cursor position to top left
	E.cx = 0;
  	E.cy = 0;
    E.numrows = 0;
    E.row = NULL;
	//since it is passed by reference, 
	//the values of E will be initialised with row and coloumn size of terminal
    if (getWindowSize(&E.screenrows, &E.screencols) == -1) 
  	    die("getWindowSize");
}


int main(int argc, char *argv[]) {

	enableRawMode();
	initEditor();
    if (argc >= 2) {
        editorOpen(argv[1]);
    }	

	while (1) {

		editorRefreshScreen();
    	editorProcessKeypress();
  }
	return 0;
}
