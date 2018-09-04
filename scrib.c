
/******************************* includes *******************************/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE


#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#include <time.h>

/****************************** defines ******************************/

#define CTRL_KEY(k) ((k) & 0x1f)
#define SCRIB_VERSION "0.0.1"
#define SCRIB_TAB_STOP 4
#define KILO_QUIT_TIMES 3

//for cursor movement
enum editorKey {
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






/******************************* data *******************************/

typedef struct erow {
  int size;
  int rsize;
  char *chars;
  char *render;//for rendering tabs
} erow;

//to store the size of terminal
struct editorConfig {

	int cx, cy;     //store cursor position
	int rx;		    // for rendering tabs
	int rowoff;     //for vertical scrolling
	int coloff;     //for horizontal scrolling
	int screenrows;
	int screencols;
	int numrows;
	erow *row;      //array of rows to store each row of text in editor
	int dirty;		//to warn user of unsaved changes
	char *filename; //to store file name
	char statusmsg[80];
	time_t statusmsg_time;
	struct termios orig_termios;  //to store original terminal attributes
};

//global struct to store size of terminal
struct editorConfig E;









/************************ prototypes *********************/
void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen();
char *editorPrompt(char *prompt, void (*callback)(char *, int));













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
//puts the cursor at bottom right of terminal window ,
//then get the cursor position and hence deduce the size of the terminal window
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

//for rendering tabs, converting cx to rx
int editorRowCxToRx(erow *row, int cx) {
  int rx = 0;
  int j;
  for (j = 0; j < cx; j++) {
	if (row->chars[j] == '\t')
	  rx += (SCRIB_TAB_STOP - 1) - (rx % SCRIB_TAB_STOP);
	rx++;
  }
  return rx;
}


//for rendering tabs, converting rx to cx
int editorRowRxToCx(erow *row, int rx) {
  int cur_rx = 0;
  int cx;
  for (cx = 0; cx < row->size; cx++) {
    if (row->chars[cx] == '\t')
      cur_rx += (SCRIB_TAB_STOP - 1) - (cur_rx % SCRIB_TAB_STOP);
    cur_rx++;
    if (cur_rx > rx) return cx;
  }
  return cx;
}


//for rendering tabs
void editorUpdateRow(erow *row) {
	int tabs = 0;
	int j;
	for (j = 0; j < row->size; j++)
		if (row->chars[j] == '\t') tabs++;
	free(row->render);
	row->render = malloc(row->size + tabs*(SCRIB_TAB_STOP-1) + 1);
	int idx = 0;
	for (j = 0; j < row->size; j++) {
		if (row->chars[j] == '\t') {
			row->render[idx++] = ' ';
			while (idx % SCRIB_TAB_STOP != 0) row->render[idx++] = ' ';
		} else {
			row->render[idx++] = row->chars[j];
		}
	}
	row->render[idx] = '\0';
	row->rsize = idx;
}




//add the line read from input file into a newly created row 
void editorInsertRow(int at, char *s, size_t len) {

  	if (at < 0 || at > E.numrows) return;
  	E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
  	memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));

	E.row[at].size = len;
	E.row[at].chars = malloc(len + 1);
	memcpy(E.row[at].chars, s, len);
	E.row[at].chars[len] = '\0';

	E.row[at].rsize = 0;
	E.row[at].render = NULL;
	editorUpdateRow(&E.row[at]);

	E.numrows++;
	E.dirty++;	//increment when changes are made
}


//free memory assigned to a row qhen row is deleted
void editorFreeRow(erow *row) {
  	free(row->render);
  	free(row->chars);
}

//deleting a row when del key is pressed at the beginning of a row
void editorDelRow(int at) {
  	
  	//if cursor is at eof, no need to delete any row
  	if (at < 0 || at >= E.numrows) 
  		return;
  	editorFreeRow(&E.row[at]);
  	memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
  	E.numrows--;
  	E.dirty++;
}


//insert a character into a particular position in a row
void editorRowInsertChar(erow *row, int at, int c) {
	if (at < 0 || at > row->size) 
			at = row->size;
	row->chars = realloc(row->chars, row->size + 2);
	memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
	row->size++;
	row->chars[at] = c;
	editorUpdateRow(row);
	E.dirty++;
}


//append row to end of previous row when del key is pressed at beginning of a row
void editorRowAppendString(erow *row, char *s, size_t len) {
  	row->chars = realloc(row->chars, row->size + len + 1);
  	memcpy(&row->chars[row->size], s, len);
  	row->size += len;
  	row->chars[row->size] = '\0';
  	editorUpdateRow(row);
  	E.dirty++;
}

//delete a character 
void editorRowDelChar(erow *row, int at) {
  	if (at < 0 || at >= row->size) return;
  	memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
  	row->size--;
  	editorUpdateRow(row);
  	E.dirty++;
}













/************************* editor operations ********************/

//takes in a char to insert at position of cursor
void editorInsertChar(int c) {

	//if the cursor is at the end of the file, we need to append a new row
	if (E.cy == E.numrows) {
		editorInsertRow(E.numrows, "", 0);
	}
	editorRowInsertChar(&E.row[E.cy], E.cx, c);
	E.cx++;
}


//add new line when enter key is pressed
void editorInsertNewline() {

	//if cursor is at beginning of row, then just add an empty line
  	if (E.cx == 0) {
  	  	editorInsertRow(E.cy, "", 0);
  	} else {	//if cursor is in middle of row, divide the row and add to next line
  	  	erow *row = &E.row[E.cy];
  	  	editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
  	  	row = &E.row[E.cy];
  	  	row->size = E.cx;
  	  	row->chars[row->size] = '\0';
  	  	editorUpdateRow(row);
  	}
  	E.cy++;
  	E.cx = 0;
}

//deletes te char left of cursor
void editorDelChar() {
  	
  	//if cursor is past eof and del key is pressed, then nothing to delete
  	if (E.cy == E.numrows) 
  		return;

  	//if cursor is at beginning and del key is pressed, 
  	//no need to append row to previous
  	if (E.cx == 0 && E.cy == 0) 
  		return;
  	erow *row = &E.row[E.cy];
  	if (E.cx > 0) {
  	  	editorRowDelChar(row, E.cx - 1);
  	  	E.cx--;
  	} else { //if cursor at beginning of row and del key is pressed
    	E.cx = E.row[E.cy - 1].size;
    	editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
    	editorDelRow(E.cy);
    	E.cy--;
  	}
}













/*********************** file i/o *************************/

//convert all the rows of editor into a single string to be written in a file
char *editorRowsToString(int *buflen) {
  	int totlen = 0;
  	int j;
  	for (j = 0; j < E.numrows; j++)
  	  	totlen += E.row[j].size + 1;
  	*buflen = totlen;
  	char *buf = malloc(totlen);
  	char *p = buf;
  	for (j = 0; j < E.numrows; j++) {
  	  	memcpy(p, E.row[j].chars, E.row[j].size);
  	  	p += E.row[j].size;
  	  	*p = '\n';
  	  	p++;
  	}
  	return buf;
}	


//open file in editor
void editorOpen(char *filename) {

	free(E.filename);
	E.filename = strdup(filename);

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
		editorInsertRow(E.numrows, line, linelen);

	}
	free(line);
	fclose(fp);

	//make number of changes to 0 on opening
	E.dirty = 0;
}




//write contents of editor into file and save
void editorSave() {

	//if its a new file, prompt for a name from user
  	if (E.filename == NULL) {
  		//if user enters filename and presses enter, save file
    	E.filename = editorPrompt("Save as: %s (ESC to cancel)", NULL);
    	//if no name is returned, i.e. user pressed esc, abort save
    	if (E.filename == NULL) {
      		editorSetStatusMessage("Save aborted");
      	return;
    }
  	}  

  	int len;
  	//get content of editor into buf
  	char *buf = editorRowsToString(&len);
  	int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
  	ftruncate(fd, len);

  	//write the contents of editor i.e. buf into file and save
  	write(fd, buf, len);
  	if (fd != -1) {	//if no error occured while opening file
    	if (ftruncate(fd, len) != -1) {	//if no error occured while truncating
      		if (write(fd, buf, len) == len) {	//if no error occured while writing	
  				close(fd);
  				free(buf);

  				//make number of changes to 0 on saving file to disk
  				E.dirty = 0;

  				//display successful save status in status bar
  				editorSetStatusMessage("%d bytes written to disk", len);
  				return;
  			}
  		}
  		close(fd);
  	}

  	free(buf);
  	//display unsuccessful save msg in status bar
  	editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));

}










/************************* find ****************************/


//Find 
void editorFindCallback(char *query, int key) {


	//enable user to got to next or previous match using arow keys
  	static int last_match = -1;
  	static int direction = 1;
  	if (key == '\r' || key == '\x1b') {
  	  	last_match = -1;
  	  	direction = 1;
  	  	return;
  	} else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
  	  	direction = 1;
  	} else if (key == ARROW_LEFT || key == ARROW_UP) {
  	  	direction = -1;
  	} else {
  	  	last_match = -1;
  	  	direction = 1;
  	}

  	if (last_match == -1) direction = 1;
  	int current = last_match;

  	int i;
  	for (i = 0; i < E.numrows; i++) {

  	  	current += direction;
    	if (current == -1) current = E.numrows - 1;
    	else if (current == E.numrows) current = 0;
    	erow *row = &E.row[current];

    	//search
  	  	char *match = strstr(row->render, query);
  	  	if (match) {
  	  		last_match = current;
      		E.cy = current;
  	  	  	E.cx = editorRowRxToCx(row, match - row->render);
  	  	  	E.rowoff = E.numrows;
  	  	  	break;
  	  	}
  	}

}


//Search query
void editorFind() {

	int saved_cx = E.cx;
  	int saved_cy = E.cy;
  	int saved_coloff = E.coloff;
  	int saved_rowoff = E.rowoff;

  	char *query = editorPrompt("Search: %s (Use ESC/Arrows/Enter)",
                             editorFindCallback);
  	if (query){
  		free(query);
  	} else {
  		//restore cursor position if search cancel, i.e. on esc 
    	E.cx = saved_cx;
    	E.cy = saved_cy;
    	E.coloff = saved_coloff;
    	E.rowoff = saved_rowoff;
    }
}











/*********************** append buffer *************************/

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

//draw status bar
void editorDrawStatusBar(struct abuf *ab) {

	//escape sequence [7m switches to inverted color
	abAppend(ab, "\x1b[7m", 4);

	//display filename in status bar
	char status[80], rstatus[80];

	//display file info 
	int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
    				E.filename ? E.filename : "[No Name]", E.numrows,
    				E.dirty ? "(modified)" : "");

	int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d",
												E.cy + 1, E.numrows);
	if (len > E.screencols) 
		len = E.screencols;
	abAppend(ab, status, len);

	while (len < E.screencols) {

		if (E.screencols - len == rlen) {
			abAppend(ab, rstatus, rlen);
			break;
		} else {

			abAppend(ab, " ", 1);
			len++;
		}
	}
	//escape sequence [7m switches back to normal color
	abAppend(ab, "\x1b[m", 3);
	abAppend(ab, "\r\n", 2);
}

//message bar
void editorDrawMessageBar(struct abuf *ab) {
  abAppend(ab, "\x1b[K", 3);
  int msglen = strlen(E.statusmsg);
  if (msglen > E.screencols) msglen = E.screencols;
  if (msglen && time(NULL) - E.statusmsg_time < 5)
	abAppend(ab, E.statusmsg, msglen);
}



//To enable scrolling when cursor moves out of window
void editorScroll() {
	
	E.rx = 0;
	if (E.cy < E.numrows) {
		E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
	}



	//vertical scroll
	if (E.cy < E.rowoff) {
		E.rowoff = E.cy;
	}
	if (E.cy >= E.rowoff + E.screenrows) {
		E.rowoff = E.cy - E.screenrows + 1;
	}
	
	//horizontal scroll
	if (E.rx < E.coloff) {
		E.coloff = E.rx;
	}
	if (E.rx >= E.coloff + E.screencols) {
		E.coloff = E.rx - E.screencols + 1;
	}
}





//Write a welcome message at 1/3 of the screen
//draw tildas at the beginning of every row except welcome msg line
//no of rows is obtained by getWindowSize and stored in E.screenrows
void editorDrawRows(struct abuf *ab) {
	int y;

	//go row by row and display each row by appending into ab 
	for (y = 0; y < E.screenrows; y++) {
		
		//to enable scrolling and start display from top row visible on scroll
		int filerow = y + E.rowoff;
		
		//if there is no more text in the file to be displayed
		if (filerow >= E.numrows) {

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

			int len = E.row[filerow].rsize - E.coloff;
			if (len < 0) len = 0;

			//if length of E.row is longer than total coloumns, truncate
			if (len > E.screencols) len = E.screencols;

			//write E.row to text buffer for display
			abAppend(ab, &E.row[filerow].render[E.coloff], len);
		}
	
		abAppend(ab, "\x1b[K", 3);
			abAppend(ab, "\r\n", 2);
	}
}

//refresh screen line by line rather than entire screen
void editorRefreshScreen() {

	editorScroll();

	struct abuf ab = ABUF_INIT;

	abAppend(&ab, "\x1b[?25l", 6);//hide the cursor
	//abAppend(&ab, "\x1b[2J", 4); //clear entire screen
	abAppend(&ab, "\x1b[H", 3);  //reposition cursor to top left corner

	editorDrawRows(&ab); //draw tildas
	editorDrawStatusBar(&ab);//draw status bar
	editorDrawMessageBar(&ab);//draw message bar

	//move cursor to position stored in cx,cy
	char buf[32];
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1,
											(E.rx - E.coloff) + 1);
	abAppend(&ab, buf, strlen(buf));

	abAppend(&ab, "\x1b[?25h", 6); //show the cursor

	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}



//status message
void editorSetStatusMessage(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
	va_end(ap);
	E.statusmsg_time = time(NULL);
}















/**************************** input *******************************/


//to prompt the user for a filename to "Save as.." when no file name was specified
char *editorPrompt(char *prompt, void (*callback)(char *, int)) {
  size_t bufsize = 128;
  char *buf = malloc(bufsize);
  size_t buflen = 0;
  buf[0] = '\0';
  while (1) {
    editorSetStatusMessage(prompt, buf);
    editorRefreshScreen();
    int c = editorReadKey();

    //if del key is pressed while entering filename in input prompt
    if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
      	if (buflen != 0) buf[--buflen] = '\0';
    } else if (c == '\x1b') {	//if esc is pressed no need to save file, delete buf and remove status msg
      	editorSetStatusMessage("");
      	if (callback) callback(buf, c);
      	free(buf);
      	return NULL;
    } else if (c == '\r') {	//if enter is pressed, return buff and remove status msg
      	if (buflen != 0) {
        	editorSetStatusMessage("");
        if (callback) callback(buf, c);
        return buf;
      }
    } else if (!iscntrl(c) && c < 128) {
      if (buflen == bufsize - 1) {
        bufsize *= 2;
        buf = realloc(buf, bufsize);
      }
      buf[buflen++] = c;
      buf[buflen] = '\0';
    }

    if (callback) callback(buf, c);
  }
}


//move the cursor with a,d,w,s
void editorMoveCursor(int key) {

	erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

	//if conditions prevent cursor from going out of window
	switch (key) {
		case ARROW_LEFT:
			if (E.cx != 0) { 
			   E.cx--;
			} else if (E.cy > 0) {
				E.cy--;
				E.cx = E.row[E.cy].size;
			}
			break;
		case ARROW_RIGHT:
			if (row && E.cx < row->size) {
				E.cx++;
			} else if (row && E.cx == row->size) {
					E.cy++;
					E.cx = 0;  
			} 
			break;
		case ARROW_UP:
			if (E.cy != 0) {
			   E.cy--;
			}
			break;
		case ARROW_DOWN:
			 //let the cursor go below the window but not more than number of text lines present  
			if (E.cy < E.numrows) {  
			   E.cy++;  
			 }
			break;
	}

	row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
	int rowlen = row ? row->size : 0;
	if (E.cx > rowlen) {
	  E.cx = rowlen;
	}
}


//read a key from terminal using editorReadKey() and handle it
void editorProcessKeypress() {

	static int quit_times = KILO_QUIT_TIMES;	
  	int c = editorReadKey();

  	switch (c) {

  	//enter key
		case '\r':
			editorInsertNewline();
			break;
	
		//quit when ctrl-q is pressed 
		case CTRL_KEY('q'): 
	
			//clear the screen and exit
			//warn if unsaved changes and quit anyway if ctrl+q pressed 3 times
			if (E.dirty && quit_times > 0) {
        		editorSetStatusMessage("WARNING!!! File has unsaved changes. "
          							"Press Ctrl-Q %d more times to quit.", quit_times);

        		//decrement quit_times every time ctrl+q is pressed to keep count until 3
        		quit_times--;
        		return;
      		}
      		write(STDOUT_FILENO, "\x1b[2J", 4);
      		write(STDOUT_FILENO, "\x1b[H", 3);
      		exit(0);
      		break;
	
		//save file when ctrl+s is pressed
		case CTRL_KEY('s'):
    	  	editorSave();
    	  	break;
	
		case HOME_KEY:
			E.cx = 0;
			break;
	
		case END_KEY:
			if (E.cy < E.numrows)
				E.cx = E.row[E.cy].size;
			break;

		//SEARCH
		case CTRL_KEY('f'):
      		editorFind();
      		break;
	
		case BACKSPACE://backspace key
		case CTRL_KEY('h')://ctrl+h
		case DEL_KEY://delete key
			if (c == DEL_KEY) 
				editorMoveCursor(ARROW_RIGHT);
      		editorDelChar();
			break;
	
		case PAGE_UP:
		case PAGE_DOWN:
		{
	
			if (c == PAGE_UP) {
				E.cy = E.rowoff;
			} else if (c == PAGE_DOWN) {
				E.cy = E.rowoff + E.screenrows - 1;
			  if (E.cy > E.numrows) E.cy = E.numrows;
			}
	
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
	
		case CTRL_KEY('l'):
    	case '\x1b':
    	  	break;
	
		//for any cases which are not mapped to special keys, 
		//insert the character into text editor
		default:
			editorInsertChar(c);
			break;
  	}
  	//reset quit_times back to 3 when user presses any other key except ctrl-q
  	quit_times = KILO_QUIT_TIMES;
}


















/******************************* init *******************************/

void initEditor() {

	//initialise cursor position to top left
	E.cx = 0;
	E.cy = 0;
	E.rx = 0;
	E.numrows = 0;
	E.row = NULL;
	E.dirty = 0;
	E.rowoff = 0;
	E.coloff = 0;
	E.filename = NULL;
	E.statusmsg[0] = '\0';
	E.statusmsg_time = 0;

	//since it is passed by reference, 
	//the values of E will be initialised with row and coloumn size of terminal
	if (getWindowSize(&E.screenrows, &E.screencols) == -1) 
		die("getWindowSize");
	E.screenrows -= 2;//make place for status bar and status message
}









///////////////////////////////// MAIN ////////////////////////////
int main(int argc, char *argv[]) {

	enableRawMode();
	initEditor();
	if (argc >= 2) {
		editorOpen(argv[1]);
	}	

	editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");

	while (1) {

		editorRefreshScreen();
		editorProcessKeypress();
  	}
	return 0;
}
