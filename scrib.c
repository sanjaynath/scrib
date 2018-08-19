
/******************************* includes *******************************/

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

/******************************* data *******************************/

struct termios orig_termios; //to store original terminal attributes


/******************************* terminal *******************************/

//error handling - errno variable stores the error
void die(const char *s) {
  perror(s);//to print out decriptive error message with tag 's' passed on to the function die
  exit(1);
}

void disableRawMode() {
	//restore original attributes of terminal on exit from text editor
	//orig_termios was used to store the original 
	//terminal attributes before enetering the program
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
    	die("tcsetattr");

}
void enableRawMode() {

	//get original attributes of terminal
	if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) 
		die("tcgetattr");

	//to call disableRawMode automatically at exit
	atexit(disableRawMode);
	struct termios raw = orig_termios; //save the original terminal attributes
	
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





/******************************* init *******************************/

int main() {
	enableRawMode();
	char c;
	
	//to print out every character read
	//while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q') {
	while (1) {
    char c = '\0';
    if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) 
    	die("read");
    if (iscntrl(c)) { 	//for control characters like ctrl-c, ctrl-a, etc.
      printf("%d\r\n", c);
    } else {
      printf("%d ('%c')\r\n", c, c);
    }
    if (c == 'q') break;
  }
	return 0;
}
