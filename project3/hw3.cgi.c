#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <ctype.h>

#define MAX_USER 5
#define F_READ 0
#define F_WRITE 1

typedef struct cf {
	int state;
	char ip[50];
	char port[20];
	char file[30];
} cf;

cf connection[MAX_USER + 1];

int space(char msg_buf[1024]) {

	int i, ans = 1;
	for (i = 0; i < strlen(msg_buf); i++) {
		if (!isspace(msg_buf[i])) {
			ans = 0;
			break;
		}
	}
	return ans;
}

int parse(char *query) {

	int count = 0, i = 0;
	char *temp;
	temp = strtok(query, "&");
	while (strlen(temp) > 10) {
		if ((strlen(temp)) == 10) {
			break;
		} else {
			strcpy(connection[count].ip, temp);
			strcpy(connection[count].port, strtok(NULL, "&"));
			strcpy(connection[count].file, strtok(NULL, "&"));
			count++;
			if (count == MAX_USER)
				break;
			temp = strtok(NULL, "&");
		}
	}
	for (i = 0; i < count; i++) {
		temp = strtok(connection[i].ip, "=");
		strcpy(connection[i].ip, strtok(NULL, "="));
		temp = strtok(connection[i].port, "=");
		strcpy(connection[i].port, strtok(NULL, "="));
		temp = strtok(connection[i].file, "=");
		strcpy(connection[i].file, strtok(NULL, "="));
		connection[i].state = F_READ;
	}
	return count;
}

int readline(int fd, char *ptr, int maxlen) {
	int n, rc;
	char c;
	*ptr = 0;
	for (n = 1; n < maxlen; n++) {
		if ((rc = read(fd, &c, 1)) == 1) {
			*ptr++ = c;
			if (c == '\n')
				break;
		} else if (rc == 0) {

			if (n == 1)
				return (0);
			else
				break;
		} else
			return (-1);
	}
	return (n);
}

void server_handler(int id, int svr_fd) {
	char buf[1024];
	char *temp = buf;
	int n;
	memset(buf, '\0', sizeof(buf));

	if (connection[id - 1].state == F_READ) {
		char ch = '~', preCh = '~';
		int	len = 1;
		
		while(1) {
			int ret = recv(svr_fd, &ch, 1, 0); 
			if (ret > 0)
			{
				if (ch == '\n') {
					printf("<script>document.all['m%d'].innerHTML += \"<br>\";</script>", id);
					fflush(stdout);
					break;
				} else {
					printf("<script>document.all['m%d'].innerHTML += \"%c\";</script>", id, ch);
					fflush(stdout);
				}				
				
				if (preCh == '%' && ch == ' ') {
					connection[id - 1].state = F_WRITE;
					break;
				} else {
					preCh = ch;
				}
			} else {
				if (errno != EAGAIN)
					sleep(1);
				else if(ret <= 0) {
					exit(0);
				}
			}
		}
		fflush(stdout);
		memset(buf, '\0', sizeof(buf));
	}
}

bool is_exit(char *exitStr, int len, char newCh) {
	for(int i = 0; i < len; i++) {
		exitStr[i] = exitStr[i + 1];
	}
	exitStr[len] = newCh;
	
	return (strstr(exitStr, "exit") != 0);
}

void client_handler(int id, FILE *fd, int svr_fd) {
	char ch = '~', preCh = '~';
	char exitStr[4] = {0};
	do {
		int n = read(fileno(fd), &ch, sizeof(ch));
		if (n > 0) {
			if (ch == '\n') {
				printf("<script>document.all['m%d'].innerHTML += \"<br>\";</script>", id);
				fflush(stdout);
				connection[id - 1].state = F_READ;
			} else {
				if(preCh == ' ' && ch == ' ')
					continue;
				else {
					printf("<script>document.all['m%d'].innerHTML += \"<b>%c</b>\";</script>", id, ch);
					fflush(stdout);
					preCh = ch;
					
					if(is_exit(exitStr, sizeof(exitStr) - 1, ch)) {
						shutdown(svr_fd, 2);
						close(svr_fd);
						fclose(fd);
						exit(1);
					}
				}
			}

			if (send(svr_fd, &ch, sizeof(ch), 0) == -1)
				exit(0);
		} else if (n == 0) {
			fclose(fd);
			connection[id - 1].state = F_READ;
		} else {
			shutdown(svr_fd, 2);
			close(svr_fd);
			fclose(fd);
			exit(0);
		}
	} while (connection[id - 1].state == F_WRITE);


}

int exe_cgi(int id) {

	fd_set readfds;
	int svr_fd;
	struct sockaddr_in client_addr;
	struct hostent *he;
	char msg_buf[4];
	int n, i, end;
	FILE *fd;

	fd = fopen(connection[id].file, "r");
	if (fd == NULL) {
		printf(
				"<script>document.all['m%d'].innerHTML +=\"Error: file('%s') doesn't exist.<br>\";</script>",
				id + 1, connection[id].file);
		exit(1);
	}
	if ((he = gethostbyname(connection[id].ip)) == NULL) {
		printf(
				"<script>document.all['m%d'].innerHTML +=\"Usage: client <server ip> <port> <testfile> <br>\";</script>",
				id + 1);
		exit(1);
	}

	svr_fd = socket(AF_INET, SOCK_STREAM, 0);

	bzero((char*) &client_addr, sizeof(client_addr));
	client_addr.sin_family = AF_INET;
	client_addr.sin_addr = *((struct in_addr*) he->h_addr);
	client_addr.sin_port = htons((u_short) atoi(connection[id].port));

	if (connect(svr_fd, (struct sockaddr*) &client_addr, sizeof(client_addr))
			== -1) {
		printf("connection failed.<br>\n");
		return 0;
	}

	int flag = fcntl(svr_fd, F_GETFL, 0);
	fcntl(svr_fd, F_SETFL, flag | O_NONBLOCK);

	sleep(1);

	char *CmdTok;
	FD_ZERO(&readfds);
	FD_SET(svr_fd, &readfds);
	while (1) {
		if (select(svr_fd + 1, &readfds, NULL, NULL, NULL) < 0)
			return 0;
		if (FD_ISSET(svr_fd, &readfds)) {
			if (connection[id].state == F_READ) {
				server_handler(id + 1, svr_fd);
			}
			if (connection[id].state == F_WRITE) {
				client_handler(id + 1, fd, svr_fd);
			}
		}
	}
}

int main(void) {

	char *input;
	int count, i;
	int pid, status;

	printf("Content-type:text/html\n\n");
	printf("<html>\n");
	printf("<head>\n");
	printf(
			"<meta http-equiv=\"Content-Type\" content=\"text/html; charset=big5\" />\n");
	printf("<title>Network Programming Homework 3</title>\n");
	printf("</head>\n");
	printf("<body bgcolor=#336699>\n");
	printf("<font face=\"Courier New\" size=2 color=#FFFF99>\n");
	printf("<table width=\"800\" border=\"1\"><tr>\n");

	input = getenv("QUERY_STRING");

	if (input == NULL)
		printf("<P>Error!");
	count = parse(input);
	fflush(stdout);
	for (i = 0; i < count; i++) {
		printf("<td>%s</td>\n", connection[i].ip);
	}
	printf("</tr><tr>\n");
	for (i = 0; i < count; i++) {
		printf("<td valign=\"top\" id=\"m%d\"></td>\n", i + 1);
	}
	printf("</tr></table>\n");
	fflush(stdout);

	if ((pid = fork()) < 0) {
		printf("Error!<br>");
	} else if (pid == 0) {
		for (i = 0; i < count; i++) {
			int cpid = fork();
			if (cpid == 0) {
				exe_cgi(i);
				exit(0);
			} else {
//				break;
			}
		}
	}
	printf("</font>\n");
	printf("</body>\n");
	printf("</html>\n");
	fflush(stdout);
	waitpid(pid, NULL, 0);

	return 0;
}

