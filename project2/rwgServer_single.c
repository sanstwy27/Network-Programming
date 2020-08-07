#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <signal.h>
#include <netinet/in.h>
#include <ctype.h>
#include <fcntl.h>
#include <arpa/inet.h>

#define D_CMD_LEN 256
#define D_LINE_RANGE 1000
#define D_BUF_SIZE 4096
#define D_SERV_TCP_PORT 7000
#define D_LISTEN_NUM 5

enum CmdType { CT_NORMAL, CT_JUMP, CT_WRITE, CT_CHAT_PIPE};

// Link List for Command Node
struct CmdNode {
	char cmd[D_CMD_LEN];
	char file[D_CMD_LEN];
	char *args[D_CMD_LEN];
	int jumpto;
	int fromChatPipe;
	bool piperr;
	CmdType type;
	struct CmdNode *next;
};

// For Client Data
struct ClientData {
	int sockfd;
	int port;
	char *ip;
	char name[30];
	char libPath[D_CMD_LEN];
	int line;
	int chatPIPE[2];
};

void yell(char message[D_CMD_LEN], struct ClientData clnData[D_LISTEN_NUM + 1]);
void tell(char message[D_CMD_LEN], struct ClientData clnData[D_LISTEN_NUM + 1], int toId);

void strip_extra_spaces(char*);
int child_cut_cmd(struct CmdNode*, int);
void exec_cmd(char * const *, char * const *);
void op_pipe(int **, int, int **, int, bool);
bool exec_standalone_cmd(int id, struct ClientData clnData[D_LISTEN_NUM + 1], int linePipe[][D_LINE_RANGE][2], struct CmdNode *current, int cmdPipe[][2], int curCmd, char libPath[2 * D_CMD_LEN]);
void child_pipe_ctrl(int id, struct ClientData clnData[D_LISTEN_NUM + 1], struct CmdNode *, int, int, int** cmdPipe, int, int linePipe[][D_LINE_RANGE][2], const char *);
void child_main(int id, struct ClientData clnData[D_LISTEN_NUM + 1], int linePipe[][D_LINE_RANGE][2]);
void child_process(int id, struct ClientData clnData[D_LISTEN_NUM + 1], struct CmdNode *, int, int linePipe[][D_LINE_RANGE][2]);

int main(int argc, char *argv[], char *envp[]) {

	int servSockfd, newSockfd, clilen, childpid;
	struct sockaddr_in cli_addr, serv_addr;
	/*
	 * Open a TCP socket (an Internet stream socket).
	 */
	if ((servSockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		perror("server: can't open stream socket");
	/*
	 * Bind our local address so that the client can send to us.
	 */
	int servPort = D_SERV_TCP_PORT;
	if(argc > 1) {
		servPort = atoi(argv[1]);
	}
	bzero((char*) &serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(servPort);
	while(bind(servSockfd, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) < 0) {
		perror("server: can't bind local address");
		sleep(1);
	}

	listen(servSockfd, D_LISTEN_NUM);
	printf("Server[%d] listening on %s:%d.\n", getpid(), inet_ntoa(serv_addr.sin_addr), servPort);

	// Init
	struct ClientData clnData[D_LISTEN_NUM + 1];
	for (int i = 0; i <= D_LISTEN_NUM; i++) {
		clnData[i].sockfd = -1;
		clnData[i].chatPIPE[0] = -1;
		clnData[i].chatPIPE[1] = -1;
	}

	int linePipe[D_LISTEN_NUM + 1][D_LINE_RANGE][2];
	for (int i = 0; i <= D_LISTEN_NUM; i++) {
		for (int j = 0; j < D_LINE_RANGE; j++) {
			linePipe[i][j][0] = -1;
			linePipe[i][j][1] = -1;
		}
	}

	char welcome[] =
			"****************************************\n"
			"** Welcome to the information server. **\n"
			"****************************************\n";
    int userNum = 0, nfds = getdtablesize();
	int stdinOriFd = dup(STDIN_FILENO);
	int stdoutOriFd = dup(STDOUT_FILENO);
	int stderrOriFd = dup(STDERR_FILENO);

	// Service
    fd_set rfds,afds;
    FD_ZERO(&afds);
    FD_SET(servSockfd, &afds);
	while(1) {
		memcpy( &rfds, &afds, sizeof(rfds));
		select(nfds, &rfds, (fd_set*) 0, (fd_set*) 0, (struct timeval*) 0);

		if(FD_ISSET(servSockfd, &rfds)) {
			clilen = sizeof(cli_addr);
			newSockfd = accept(servSockfd, (struct sockaddr*) &cli_addr, (socklen_t*) &clilen);
			if (newSockfd < 0)
				perror("server: accept error");
			else {
				if (userNum + 1 > D_LISTEN_NUM)
					close(newSockfd);
				else {
					int i = 1;
					for(i = 1; i <= D_LISTEN_NUM && clnData[i].sockfd != -1; i++);
					clnData[i].sockfd = newSockfd;
					strcpy(clnData[i].name, "(no name)");
					strcpy(clnData[i].libPath, "bin:.");
					clnData[i].ip = inet_ntoa(cli_addr.sin_addr);
					clnData[i].port = ntohs(cli_addr.sin_port);
					clnData[i].line = 0;
					clnData[i].chatPIPE[0] = -1;
					clnData[i].chatPIPE[1] = -1;

					write(clnData[i].sockfd, welcome, strlen(welcome));
					char temp[D_CMD_LEN] = {0};
					sprintf(temp, "*** User '(no name)' entered from %s/%d. ***\n", clnData[i].ip, clnData[i].port);
					yell(temp, clnData);
					write(clnData[i].sockfd, "% ", 2);
					dup2(stdoutOriFd, STDOUT_FILENO);
					FD_SET(newSockfd, &afds);
					++userNum;
				}
			}
		}

		for (int i = 1; i <= D_LISTEN_NUM; i++) {
			if (clnData[i].sockfd > 0 && FD_ISSET(clnData[i].sockfd, &rfds)) {
				dup2(stdinOriFd, STDIN_FILENO);
				dup2(stdoutOriFd, STDOUT_FILENO);
				dup2(stderrOriFd, STDERR_FILENO);
				
				int backfd = clnData[i].sockfd;
				child_main(i, clnData, linePipe);
				if (clnData[i].sockfd == -1) {
					--userNum;
					shutdown(backfd, 2);
					close(backfd);
					FD_CLR(backfd, &afds);
				}
				fflush(stdout);
			}
		}
	}

}

void yell(char message[D_CMD_LEN], struct ClientData clnData[D_LISTEN_NUM + 1]) {
	for (int i = 1; i <= D_LISTEN_NUM; i++) {
		if (clnData[i].sockfd > 0) {
			write(clnData[i].sockfd, message, strlen(message));
		}
	}
}

void tell(char message[D_CMD_LEN], struct ClientData clnData[D_LISTEN_NUM + 1], int toId) {
	dup2(clnData[toId].sockfd, STDOUT_FILENO);
	printf("%s", message);
	fflush(stdout);
}

void child_main(int id, struct ClientData clnData[D_LISTEN_NUM + 1], int linePipe[][D_LINE_RANGE][2]) {

	int sockfd = clnData[id].sockfd;
	fflush (stdout);
	fflush (stderr);

	struct CmdNode *head = NULL;
	struct CmdNode *newCmd = NULL;
	struct CmdNode *prev = NULL;

	int n = 0;
	char ch = '~', preCh = '~';
	char temp[D_CMD_LEN] = {0};

	printf("Client[%d] ", id);
	while ((n = read(sockfd, &ch, 1)) == 1) {
		printf("%c", ch);
		
		if (ch != '|' && ch != '\n' && ch != '\r' && ch != '\t') {
			if(!isspace(preCh) || !isspace(ch)) {
				sprintf(temp, "%s%c", temp, ch);
			}
			preCh = ch;
			continue;
		}
		if (temp[strlen(temp) - 1] == '>' && ch == '|') {
			sprintf(temp, "%s%c", temp, ch);
		}

		if (strlen(temp) != 0) {
			newCmd = (struct CmdNode*) malloc(sizeof(struct CmdNode));
			strcpy(newCmd->cmd, temp);
			if (!head) {
				head = newCmd;
				prev = newCmd;
			} else {
				prev->next = newCmd;
				prev = newCmd;
			}
			memset(temp, '\0', sizeof(temp));
		}

		if (ch == '\n') {
			if (head) {
				// Parsing here
				int totalCmd = child_cut_cmd(head, clnData[id].line);
				// Fork & Pipe here
				setenv("PATH", clnData[id].libPath, 1);
				child_process(id, clnData, head, totalCmd, linePipe);

				// New Line
				head = NULL;
				newCmd = NULL;
				prev = NULL;
				clnData[id].line = (++clnData[id].line) % D_LINE_RANGE;
			}
			break;
		}
	}
	if (clnData[id].sockfd > 0) {
		write(sockfd, "% ", 2);
	}
}

void strip_extra_spaces(char* str) {
	int i, x;
	for (i = x = 0; str[i]; ++i)
		if (!isspace(str[i]) || (i > 0 && !isspace(str[i - 1])))
			str[x++] = str[i];
	if(i > 0 && isspace(str[i - 1])) --x;
	str[x] = '\0';
}

int child_cut_cmd(struct CmdNode *head, int line) {
	struct CmdNode *current = NULL;

	int totalCmd = 0;
	// Deal with Continuous Space
	current = head;
	while (current) {
		++totalCmd;
		strip_extra_spaces(current->cmd);
		current = current->next;
	}

	// Split
	current = head;
	while (current) {
		current->type = CT_NORMAL;
		current->piperr = false;
		current->jumpto = -1;
		current->fromChatPipe = 0;

		int argc = 0;
		struct CmdNode* newNode = NULL;
		char jumpstr[D_CMD_LEN] = {0};
		const char *delim = " ";
		char *pch = strtok(current->cmd, delim);
		while (pch != NULL) {
			switch(*pch) {
				case '0':
				case '1':
				case '2':
				case '3':
				case '4':
				case '5':
				case '6':
				case '7':
				case '8':
				case '9':
					if(argc > 0) {
						current->args[argc++] = pch;
					} else {
						sprintf(jumpstr, "%s", pch);
						if(atoi(jumpstr) < D_LINE_RANGE) {
							current->type = CT_JUMP;
						}
						current->jumpto = (atoi(jumpstr) + line) % D_LINE_RANGE;
					}
					break;
				case '!':
					if(strstr(current->args[0], "name") != NULL ||
							strstr(current->args[0], "tell") != NULL ||
							strstr(current->args[0], "yell") != NULL) {
						current->args[argc++] = pch;
					} else {
						// jump error
						sprintf(jumpstr, "%s", pch + 1);
						newNode = (struct CmdNode*) malloc(sizeof(struct CmdNode));
						strcpy(newNode->cmd, jumpstr);
						newNode->next = current->next;
						current->next = newNode;
						current->piperr = true;
					}
					break;
				case '>':
					if(strstr(current->args[0], "name") != NULL ||
							strstr(current->args[0], "tell") != NULL ||
							strstr(current->args[0], "yell") != NULL) {
						current->args[argc++] = pch;
					} else if(strlen(pch) > 1 && *(pch + 1) == '|') {
						current->type = CT_CHAT_PIPE;
					} else {
						// write file
						pch = strtok(NULL, delim);
						strcpy(current->file, pch);
						current->type = CT_WRITE;
					}
					break;
				case '<':
					if(strstr(current->args[0], "name") != NULL ||
							strstr(current->args[0], "tell") != NULL ||
							strstr(current->args[0], "yell") != NULL) {
						current->args[argc++] = pch;
					} else if(strlen(pch) > 1 && isdigit(*(pch + 1))) {
						int fromChatPipe = atoi(pch + 1);
						current->fromChatPipe = fromChatPipe;
					}
					break;
				default:
					current->args[argc++] = pch;
					break;
			}
			pch = strtok(NULL, delim);
		}

		current = current->next;
	}


	struct CmdNode *x;
	current = head;
	while (current && current->next != NULL) {
		if(current->next->type == CT_JUMP) {
			current->type = current->next->type;
		}
		current->jumpto = current->next->jumpto;
		x = current;
		current = current->next;
	}
	if (current && (current->jumpto >= 0)) {
		--totalCmd;
		x->next = NULL;
		free(current);
	}

	return totalCmd;
}

void exec_cmd(int sockfd, const char *path, char * const *cmd) {
	if (execvp(path, cmd) == -1) {
		dup2(sockfd, STDOUT_FILENO);
		printf("Unknown command: [%s].\n", cmd[0]);
		exit(0);
	}
}

void op_pipe(int fromPipe[][2], int fNo, int toPipe[][2], int tNo, bool dumpError) {
	if(toPipe) {
		close(toPipe[tNo][0]);
		if (dumpError) {
			dup2(toPipe[tNo][1], STDERR_FILENO);
		}
		dup2(toPipe[tNo][1], STDOUT_FILENO);
	}
	if(fromPipe) {
		if (fromPipe[fNo][0] != -1) {
			close(fromPipe[fNo][1]);
			dup2(fromPipe[fNo][0], STDIN_FILENO);
		}
	}
}

void child_pipe_ctrl(int id, struct ClientData clnData[D_LISTEN_NUM + 1], struct CmdNode *current, int no, int totalCmd, int cmdPipe[][2], int linePipe[][D_LINE_RANGE][2], const char *libPath) {
	int line = clnData[id].line;
	int jumpnum = current->jumpto;
	int writefd;

	int fromChatPipe = current->fromChatPipe;
	if (current->fromChatPipe > 0) {
		if ((clnData[fromChatPipe].chatPIPE[0] > 0 && clnData[fromChatPipe].chatPIPE[1] > 0) ||
				(clnData[fromChatPipe].chatPIPE[0] == -1 && clnData[fromChatPipe].chatPIPE[1] == -1)) {
			free(current);
			exit(0);
		}
	}

	if (no == 0) {
		if (current->type == CT_JUMP) {
			op_pipe(NULL, -1, linePipe[id], jumpnum, current->piperr);
			if(linePipe[id][line][0] != -1) {
				op_pipe(linePipe[id], line, NULL, -1, current->piperr);
			}
			if (fromChatPipe > 0) {
				if(clnData[fromChatPipe].chatPIPE[1] != -1)
					close(clnData[fromChatPipe].chatPIPE[1]);
				dup2(clnData[fromChatPipe].chatPIPE[0],	STDIN_FILENO);
			}
			exec_cmd(clnData[id].sockfd, libPath, current->args);
		} else if (current->type == CT_NORMAL) {
			if (no < totalCmd - 1) {
				op_pipe(NULL, -1, cmdPipe, no + 1, current->piperr);
			}
			if(linePipe[id][line][0] != -1) {
				op_pipe(linePipe[id], line, NULL, -1, current->piperr);
			}
			if (fromChatPipe > 0) {
				if(clnData[fromChatPipe].chatPIPE[1] != -1) {
					close(clnData[fromChatPipe].chatPIPE[1]);
				}
				dup2(clnData[fromChatPipe].chatPIPE[0],	STDIN_FILENO);
			}
			exec_cmd(clnData[id].sockfd, libPath, current->args);
		} else if (current->type == CT_WRITE) {
			writefd = open(current->file, O_WRONLY | O_CREAT, 0666);
			dup2(writefd, STDOUT_FILENO);
			if (linePipe[id][line][0] != -1) {
				op_pipe(linePipe[id], line, NULL, -1, current->piperr);
			}
			if (fromChatPipe > 0) {
				if(clnData[fromChatPipe].chatPIPE[1] != -1)
					close(clnData[fromChatPipe].chatPIPE[1]);
				dup2(clnData[fromChatPipe].chatPIPE[0],	STDIN_FILENO);
			}
			exec_cmd(clnData[id].sockfd, libPath, current->args);
		} else if (current->type == CT_CHAT_PIPE) {
			if(clnData[id].chatPIPE[1] > 0) {
				close(clnData[id].chatPIPE[0]);
				if (current->piperr == true) {
					dup2(clnData[id].chatPIPE[1], STDERR_FILENO);
				}
				dup2(clnData[id].chatPIPE[1], STDOUT_FILENO);
			} else {
				free(current);
				exit(0);
			}
			if(linePipe[id][line][0] != -1) {
				op_pipe(linePipe[id], line, NULL, -1, current->piperr);
			}
			if (current->fromChatPipe > 0) {
				close(clnData[fromChatPipe].chatPIPE[1]);
				dup2(clnData[fromChatPipe].chatPIPE[0],	STDIN_FILENO);
			}
			exec_cmd(clnData[id].sockfd, libPath, current->args);
		}
	} else if (no < totalCmd - 1) {
		op_pipe(cmdPipe, no, cmdPipe, no + 1, current->piperr);
		exec_cmd(clnData[id].sockfd, libPath, current->args);
	} else { // (no == totalCmd - 1)
		if (current->type == CT_JUMP) {
			op_pipe(NULL, -1, linePipe[id], jumpnum, current->piperr);
			if(cmdPipe[no][0] != -1) {
				op_pipe(cmdPipe, no, NULL, -1, current->piperr);
			}
			exec_cmd(clnData[id].sockfd, libPath, current->args);
		} else if (current->type == CT_NORMAL) {
			if (cmdPipe[no][0] != -1) {
				op_pipe(cmdPipe, no, NULL, -1, current->piperr);
			}
			exec_cmd(clnData[id].sockfd, libPath, current->args);
		} else if (current->type == CT_WRITE) {
			writefd = open(current->file, O_WRONLY | O_CREAT, 0666);
			dup2(writefd, STDOUT_FILENO);
			if (cmdPipe[no][0] != -1) {
				op_pipe(cmdPipe, no, NULL, -1, current->piperr);
			}
			exec_cmd(clnData[id].sockfd, libPath, current->args);
		} else if (current->type == CT_CHAT_PIPE) {
			if(clnData[id].chatPIPE[1] > 0) {
				close(clnData[id].chatPIPE[0]);
				if (current->piperr == true) {
					dup2(clnData[id].chatPIPE[1], STDERR_FILENO);
				}
				dup2(clnData[id].chatPIPE[1], STDOUT_FILENO);
			} else {
				free(current);
				exit(0);
			}
			if(cmdPipe[no][0] != -1) {
				op_pipe(cmdPipe, no, NULL, -1, current->piperr);
			}
			exec_cmd(clnData[id].sockfd, libPath, current->args);
		}
	}
	free(current);
	exit(0);
}

bool exec_standalone_cmd(int id, struct ClientData clnData[D_LISTEN_NUM + 1], int linePipe[][D_LINE_RANGE][2], struct CmdNode *current, int cmdPipe[][2], int curCmd, char libPath[2 * D_CMD_LEN]) {
	bool hit = false;
	if (strstr(current->args[0], "exit") != 0) {
		for (int i = 0; i < D_LINE_RANGE; i++) {
			close(linePipe[id][i][0]);
			close(linePipe[id][i][1]);
		}
		close(clnData[id].chatPIPE[0]);
		close(clnData[id].chatPIPE[1]);
		clnData[id].chatPIPE[0] = -1;
		clnData[id].chatPIPE[1] = -1;
		clnData[id].ip = NULL;
		clnData[id].port = 0;
		clnData[id].sockfd = -1;
		char message[D_CMD_LEN] = {0};
		sprintf(message, "*** User '%s' left. ***\n", clnData[id].name);
		yell(message, clnData);
		hit = true;
	} else if (strstr(current->args[0], "printenv") != 0
			&& strstr(current->args[1], "PATH") != 0) {
		char *s = getenv("PATH");
		printf("PATH=%s\n", s);
		fflush(stdout);
		close(cmdPipe[curCmd][0]);
		close(cmdPipe[curCmd][1]);
		cmdPipe[curCmd][0] = -1;
		cmdPipe[curCmd][1] = -1;
		hit = true;
	} else if (strstr(current->args[0], "setenv") != 0
			&& strstr(current->args[1], "PATH") != 0) {
		setenv("PATH", current->args[2], 1);
		sprintf(libPath, "./%s/%s", current->args[2], current->args[0]);
		strcpy(clnData[id].libPath, current->args[2]);
		close(cmdPipe[curCmd][0]);
		close(cmdPipe[curCmd][1]);
		cmdPipe[curCmd][0] = -1;
		cmdPipe[curCmd][1] = -1;
		hit = true;
	} else if (strstr(current->cmd, "/") != 0) {
		perror("All arguments MUST NOT INCLUDE \"/\" for security.\n");
		memset(current, '\0', sizeof(current));
		close(cmdPipe[curCmd][0]);
		close(cmdPipe[curCmd][1]);
		cmdPipe[curCmd][0] = -1;
		cmdPipe[curCmd][1] = -1;
		hit = true;
	} else if (strstr(current->args[0], "name") != 0) {
		strcpy(clnData[id].name, current->args[1]);
		char message[D_CMD_LEN] = {0};
		sprintf(message, "*** User from %s/%d is named '%s'. ***\n", clnData[id].ip, clnData[id].port, clnData[id].name);
		yell(message, clnData);
		hit = true;
	} else if (strstr(current->args[0], "who") != 0) {
		printf("<ID>\t<nickname>\t<IP/port>\t<indicate me>\n");
		for (int i = 1; i <= D_LISTEN_NUM; i++) {
			if (clnData[i].sockfd > 0) {
				if (i == id) {
					printf("%d\t%s\t%s/%d\t<-me\n", i, clnData[i].name,
							clnData[i].ip, clnData[i].port);
				} else {
					printf("%d\t%s\t%s/%d\n", i, clnData[i].name,
							clnData[i].ip, clnData[i].port);
				}
				fflush(stdout);
			}
		}
		hit = true;
	} else if (strstr(current->args[0], "tell") != 0) {
		if(clnData[atoi(current->args[1])].sockfd <= 0 || atoi(current->args[1]) > D_LISTEN_NUM) {
			printf("*** Error: user #%s does not exist yet. ***\n", current->args[1]);
		} else {
			char tmp[D_CMD_LEN] = {0};
			for (int i = 2; current->args[i] != NULL; i++) {
				strcat(tmp, current->args[i]);
				if (current->args[i + 1] != NULL) {
					strcat(tmp, " ");
				}
			}
			char msg[D_CMD_LEN] = {0};
			sprintf(msg, "*** %s told you ***:  %s\n", clnData[id].name, tmp);
			tell(msg, clnData, atoi(current->args[1]));
		}
		hit = true;
	} else if (strstr(current->args[0], "yell") != 0) {
		char tmp[D_CMD_LEN] = {0};
		for (int i = 1; current->args[i] != NULL; i++) {
			strcat(tmp, current->args[i]);
			if (current->args[i + 1] != NULL) {
				strcat(tmp, " ");
			}
		}

		char msg[D_CMD_LEN] = {0};
		sprintf(msg, "*** %s yelled ***: %s\n", clnData[id].name, tmp);
		yell(msg, clnData);
		hit = true;
	}

	return hit;
}

void child_process(int id, struct ClientData clnData[D_LISTEN_NUM + 1], struct CmdNode *head, int totalCmd, int linePipe[][D_LINE_RANGE][2]) {

	int backOutFd = dup(STDOUT_FILENO);
	int line = clnData[id].line;

	// 2-dim pipe array
	int cmdPipe[totalCmd + 1][2] = {0};
	for (int i = 0; i < totalCmd + 1; i++) {
		cmdPipe[i][0] = -1;
		cmdPipe[i][1] = -1;
	}
	pipe(cmdPipe[0]);

	if(clnData[id].chatPIPE[0] == -1 && clnData[id].chatPIPE[1] == -1) {
		pipe(clnData[id].chatPIPE);
	}

	int sockfd = clnData[id].sockfd;
	int i = 0, pid = 0;
	struct CmdNode *current = head;
	while (current) {
		dup2(sockfd, STDIN_FILENO);
		dup2(sockfd, STDOUT_FILENO);
		dup2(sockfd, STDERR_FILENO);
		
		char libPath[2 * D_CMD_LEN] = {0};
		if(strstr(clnData[id].libPath, "bin") != NULL) {
			sprintf(libPath, "./bin/%s", current->args[0]);
		} else {
			sprintf(libPath, "./%s/%s", clnData[id].libPath, current->args[0]);
		}

		if(exec_standalone_cmd(id, clnData, linePipe, current, cmdPipe, i, libPath)) {
			break;
		} else {
			// Commend Pipe
			if (cmdPipe[i + 1][0] == -1 && cmdPipe[i + 1][1] == -1) {
				pipe(cmdPipe[i + 1]);
			}
			close(cmdPipe[i][1]);
			cmdPipe[i][1] = -1;

			// Line Pipe
			if (current->type != CT_WRITE) {
				int jumpnum = current->jumpto;
				if (jumpnum >= 0 && jumpnum < D_LINE_RANGE) {
					if (linePipe[id][jumpnum][0] == -1 && linePipe[id][jumpnum][1] == -1) {
						pipe(linePipe[id][jumpnum]);
					}
				}
			}
			close(linePipe[id][line][1]);
			linePipe[id][line][1] = -1;

			// Fork & Process Here
			pid = -1;
			while (pid == -1) {
				pid = fork();
			}
			if (pid > 0) {
				// Parent
				waitpid(pid, NULL, 0);

				int fromChatPipe = current->fromChatPipe;
				if (fromChatPipe > 0) {
					if ((clnData[fromChatPipe].chatPIPE[0] > 0 && clnData[fromChatPipe].chatPIPE[1] > 0) ||
							(clnData[fromChatPipe].chatPIPE[0] == -1 && clnData[fromChatPipe].chatPIPE[1] == -1)) {
						printf("*** Error: the pipe from #%d does not exist yet. ***\n", fromChatPipe);
					} else {
						char tmp[D_CMD_LEN] = {0};
						for(int i = 0; current->args[i] != NULL; i++) {
							strcat(tmp, current->args[i]);
							strcat(tmp, " ");
						}

						char msg[2 * D_CMD_LEN] = {0};
						sprintf(msg, "*** %s (#%d) just received the pipe from %s (#%d) by '%s<%d' ***\n",
								clnData[id].name, id, clnData[fromChatPipe].name, fromChatPipe,
								tmp, fromChatPipe);
						yell(msg, clnData);

						close(clnData[fromChatPipe].chatPIPE[0]);
						clnData[fromChatPipe].chatPIPE[0] = -1;
					}
				}

				if(current->type == CT_CHAT_PIPE) {
					if(clnData[id].chatPIPE[0] > 0 && clnData[id].chatPIPE[1] > 0) {
						char tmp[D_CMD_LEN] = {0};
						for(int i = 0; current->args[i] != NULL; i++) {
							strcat(tmp, current->args[i]);
							strcat(tmp, " ");
						}
						strcat(tmp, ">|");

						char msg[2 * D_CMD_LEN] = {0};
						sprintf(msg, "*** %s (#%d) just piped '%s' into his/her pipe. ***\n", clnData[id].name, id, tmp);
						yell(msg, clnData);

						close(clnData[id].chatPIPE[1]);
						clnData[id].chatPIPE[1] = -1;
					} else {
						printf("*** Error: your pipe already exists. ***\n");
					}
				}

				if (i == 0) {
					close(linePipe[id][line][0]);
					linePipe[id][line][0] = -1;
				}
				close(cmdPipe[i][0]);
				cmdPipe[i][0] = -1;
			} else if (pid == 0) {
				// Child
				child_pipe_ctrl(id, clnData, current, i, totalCmd, cmdPipe, linePipe, libPath);
			}
		}

		++i;
		current = current->next;
	}
	for (i = 0; i < (totalCmd + 1); i++) {
		if(cmdPipe[i][0] >= 0)
			close(cmdPipe[i][0]);
		if(cmdPipe[i][1] >= 0)
			close(cmdPipe[i][1]);
	}
	dup2(backOutFd, STDOUT_FILENO);
	fflush(stdout);
}
