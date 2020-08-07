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
	char socksIp[50];
	char socksPort[20];
} cf;

cf connection[MAX_USER + 1];

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
			strcpy(connection[count].socksIp, strtok(NULL, "&"));
			strcpy(connection[count].socksPort, strtok(NULL, "&"));
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
		temp = strtok(connection[i].socksIp, "=");
		temp = strtok(NULL, "=");
		if(temp != NULL) {
			strcpy(connection[i].socksIp, temp);
		} else {
			connection[i].socksIp[0] = '\0';
		}
		temp = strtok(connection[i].socksPort, "=");
		temp = strtok(NULL, "=");
		if(temp != NULL) {
			strcpy(connection[i].socksPort, temp);
		} else {
			connection[i].socksPort[0] = '\0';
		}
		connection[i].state = F_READ;
	}
	return count;
}

void server_handler(int id, int svr_fd) {
	char buf[1024];
	char *temp, *CmdTok;
	int n;
	memset(buf, '\0', sizeof(buf));

	recv(svr_fd, buf, sizeof(buf), 0);
	if (connection[id - 1].state == F_READ) {
		for (temp = strtok(buf, "\n"); temp; temp = strtok(NULL, "\n")) {
			if (temp[strlen(temp) - 1] == '\r')
				temp[strlen(temp) - 1] = '\0';
			if (strncmp(temp, "% ", 2)) {
				printf(
						"<script>document.all['m%d'].innerHTML += \"%s<br>\";</script>\n",
						id, temp);
				fflush(stdout);
			} else {
				connection[id - 1].state = F_WRITE;
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

	printf("<script>document.all['m%d'].innerHTML += \"<b>%% </b>\";</script>", id);
	fflush(stdout);
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

int get_svr_fd(int id) {

	char svrIp[50] = {0};
	int svrPort = -1;
	if(strlen(connection[id].socksIp) > 0) {
		strcpy(svrIp, connection[id].socksIp);
		svrPort = atoi(connection[id].socksPort);
	} else {
		strcpy(svrIp, connection[id].ip);
		svrPort = atoi(connection[id].port);
	}

	struct hostent *he;
	if ((he = gethostbyname(svrIp)) == NULL) {
		printf(
				"<script>document.all['m%d'].innerHTML +=\"Usage: client <server ip> <port> <testfile> <br>\";</script>",
				id + 1);
		exit(1);
	}

	int svr_fd = socket(AF_INET, SOCK_STREAM, 0);

	struct sockaddr_in client_addr, server_addr;
	bzero((char*) &client_addr, sizeof(client_addr));
	client_addr.sin_family = AF_INET;
	client_addr.sin_addr = *((struct in_addr*) he->h_addr);
	client_addr.sin_port = htons((u_short) svrPort);

	if (connect(svr_fd, (struct sockaddr*) &client_addr, sizeof(client_addr))
			== -1) {
		printf("connection failed.<br>\n");
		return 0;
	}

	int flag = fcntl(svr_fd, F_GETFL, 0);
	fcntl(svr_fd, F_SETFL, flag | O_NONBLOCK);
	sleep(1);

	// --- project4 add ---
	if(strlen(connection[id].socksIp) > 0) {
		char request[32];
		if ((he = gethostbyname(connection[id].ip)) == NULL) {
			printf(
					"<script>document.all['m%d'].innerHTML +=\"Usage : client <server ip> <port> <testfile> <br>\";</script>",
					id + 1);
			exit(1);
		}
		bzero(&server_addr, sizeof(server_addr));
		server_addr.sin_family = AF_INET;
		server_addr.sin_addr = *((struct in_addr*) he->h_addr);
		server_addr.sin_port = htons((u_short) atoi(connection[id].port));

		char reply[32];
		reply[0] = 4;
		reply[1] = 1;
		memcpy(&reply[2], &server_addr.sin_port, 2);
		memcpy(&reply[4], &server_addr.sin_addr.s_addr, 4);
		memcpy(&reply[8], "chinghsiangk", 12);
		reply[20] = 0;
		write(svr_fd, reply, 21);
	}
	// --- project4 end ---

	return svr_fd;
}

int exe_cgi(int id) {

	FILE *fd;
	fd = fopen(connection[id].file, "r");
	if (fd == NULL) {
		printf(
				"<script>document.all['m%d'].innerHTML +=\"Error: file('%s') doesn't exist.<br>\";</script>",
				id + 1, connection[id].file);
		exit(1);
	}

	int svr_fd = get_svr_fd(id);

	fd_set readfds;
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
			pid = fork();
			if (pid == 0) {
				exe_cgi(i);
				return 0;
			} else {
//				break;
			}
		}
		while (wait(&status) != pid)
			;
		return 0;
	}
	printf("</font>\n");
	printf("</body>\n");
	printf("</html>\n");
	fflush(stdout);

	return 0;
}

