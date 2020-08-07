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

enum CmdType { CT_NORMAL, CT_JUMP, CT_WRITE};

// Link List for Command Node
struct CmdNode {
	char cmd[D_CMD_LEN];
	char file[D_CMD_LEN];
	char *args[D_CMD_LEN];
	int jumpto;
	bool piperr;
	CmdType type;
	struct CmdNode *next;
};

void strip_extra_spaces(char*);
int child_cut_cmd(struct CmdNode*, int);
void exec_cmd(char * const *, char * const *);
void op_pipe(int **, int, int **, int, bool);
void child_pipe_ctrl(struct CmdNode *, int, int, int** cmdPipe, int, int linePipe[][2], const char *);
void child_main(int);
void child_process(struct CmdNode *, int, int, int linePipe[][2], char*);

int main(int argc, char *argv[], char *envp[]) {

	int sockfd, newsockfd, clilen, childpid;
	struct sockaddr_in cli_addr, serv_addr;
	/*
	 * Open a TCP socket (an Internet stream socket).
	 */
	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
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
	while(bind(sockfd, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) < 0) {
		perror("server: can't bind local address");
		sleep(1);
	}

	listen(sockfd, D_LISTEN_NUM);
	printf("Server[%d] listening on %s:%d.\n", getpid(), inet_ntoa(serv_addr.sin_addr), servPort);
	while (1) {
		clilen = sizeof(cli_addr);
		newsockfd = accept(sockfd, (struct sockaddr*) &cli_addr, (socklen_t*) &clilen);
		if (newsockfd < 0)
			perror("server: accept error");
		if ((childpid = fork()) < 0)
			perror("server: fork error");
		else if (childpid == 0) { /* child process */
			/* close original socket */
			close(sockfd);
			/* process the request */
			char buffer[INET_ADDRSTRLEN];
			//inet_ntop( AF_INET, &(cli_addr.sockaddr_in.sin_addr), buffer, sizeof( buffer ));
			printf( "Client[%d] %s:%d Login.\n", getpid(), inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));
			child_main(newsockfd);
			exit(0);
		} else {
			printf("Server[%d] listening on %s:%d.\n", getpid(), inet_ntoa(serv_addr.sin_addr), servPort);
		}
		close(newsockfd); /* parent process */
	}

}

void child_main(int sockfd) {

	int stdinOriFd = dup(STDIN_FILENO);
	int stdoutOriFd = dup(STDOUT_FILENO);

	dup2(sockfd, STDIN_FILENO);
	dup2(sockfd, STDOUT_FILENO);
	dup2(sockfd, STDERR_FILENO);

	printf("****************************************\n");
	printf("** Welcome to the information server. **\n");
	printf("****************************************\n");
	fflush(stdout);

	// Default Lib Path
	char path[D_CMD_LEN];
	strcpy(path, "bin");
	setenv("PATH", "bin:.", 1);

	// For Pipe & Fork
	int linePipe[D_LINE_RANGE][2];
	for (int i = 0; i < D_LINE_RANGE; i++) {
		linePipe[i][0] = -1;
		linePipe[i][1] = -1;
	}

	struct CmdNode *head = NULL;
	struct CmdNode *newCmd = NULL;
	struct CmdNode *prev = NULL;

	int line = 0, n = 0;
	char ch = '~', preCh = '~';
	char temp[D_CMD_LEN] = {0};

	write(sockfd, "% ", 2);
	dup2(stdoutOriFd, STDOUT_FILENO);
	printf("Client[%d] ", getpid());
	while (1) {
		while ((n = read(sockfd, &ch, 1)) == 1) {
			printf("%c", ch);

			if (ch != '|' && ch != '\n' && ch != '\r' && ch != '\t') {
				if(!isspace(preCh) || !isspace(ch)) {
					sprintf(temp, "%s%c", temp, ch);
				}
				preCh = ch;
				continue;
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
				if (strstr(newCmd->cmd, "exit") != 0) {
					for (int i = 0; i < D_LINE_RANGE; i++) {
						close(linePipe[i][0]);
						close(linePipe[i][1]);
					}
					exit(0);
				}
				if (head) {
					dup2(sockfd, STDOUT_FILENO);
					// Parsing here
					int totalCmd = child_cut_cmd(head, line);
					// Fork & Pipe here
					child_process(head, line, totalCmd, linePipe, path);

					// New Line
					head = NULL;
					newCmd = NULL;
					prev = NULL;
					line = (++line) % D_LINE_RANGE;
				}
				write(STDOUT_FILENO, "% ", 2);
				dup2(stdoutOriFd, STDOUT_FILENO);
				printf("Client[%d] ", getpid());
			}
		}
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
					// Parameter
					sprintf(jumpstr, "%s", pch + 1);
					newNode = (struct CmdNode*) malloc(sizeof(struct CmdNode));
					strcpy(newNode->cmd, jumpstr);
					newNode->next = current->next;
					current->next = newNode;
					current->piperr = true;
					break;
				case '>':
					// Parameter
					pch = strtok(NULL, delim);
					strcpy(current->file, pch);
					current->type = CT_WRITE;
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

void exec_cmd(const char *path, char * const *cmd) {
	if (execvp(path, cmd) == -1) {
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

void child_pipe_ctrl(struct CmdNode *curent, int no, int totalCmd, int cmdPipe[][2], int line, int linePipe[][2], const char *libPath) {
	int jumpnum = curent->jumpto;
	int writefd;

	if (no == 0) {
		if (curent->type == CT_JUMP) {
			op_pipe(NULL, -1, linePipe, jumpnum, curent->piperr);
			if(linePipe[line][0] != -1) {
				op_pipe(linePipe, line, NULL, -1, curent->piperr);
			}
			exec_cmd(libPath, curent->args);
		} else if (curent->type == CT_NORMAL) {
			if (no < totalCmd - 1) {
				op_pipe(NULL, -1, cmdPipe, no + 1, curent->piperr);
			}
			if(linePipe[line][0] != -1) {
				op_pipe(linePipe, line, NULL, -1, curent->piperr);
			}
			exec_cmd(libPath, curent->args);
		} else if (curent->type == CT_WRITE) {
			writefd = open(curent->file, O_WRONLY | O_CREAT, 0666);
			dup2(writefd, STDOUT_FILENO);
			if (linePipe[line][0] != -1) {
				op_pipe(linePipe, line, NULL, -1, curent->piperr);
			}
			exec_cmd(libPath, curent->args);
		}
	} else if (no < totalCmd - 1) {
		op_pipe(cmdPipe, no, cmdPipe, no + 1, curent->piperr);
		exec_cmd(libPath, curent->args);
	} else { // (no == totalCmd - 1)
		if (curent->type == CT_JUMP) {
			op_pipe(NULL, -1, linePipe, jumpnum, curent->piperr);
			if(cmdPipe[no][0] != -1) {
				op_pipe(cmdPipe, no, NULL, -1, curent->piperr);
			}
			exec_cmd(libPath, curent->args);
		} else if (curent->type == CT_NORMAL) {
			if (cmdPipe[no][0] != -1) {
				op_pipe(cmdPipe, no, NULL, -1, curent->piperr);
			}
			exec_cmd(libPath, curent->args);
		} else if (curent->type == CT_WRITE) {
			writefd = open(curent->file, O_WRONLY | O_CREAT, 0666);
			dup2(writefd, STDOUT_FILENO);
			if (cmdPipe[no][0] != -1) {
				op_pipe(cmdPipe, no, NULL, -1, curent->piperr);
			}
			exec_cmd(libPath, curent->args);
		}
	}
	free(curent);
	exit(0);
}

void child_process(struct CmdNode *head, int line, int totalCmd, int linePipe[][2], char *path) {

	// 2-dim pipe array
	int cmdPipe[totalCmd + 1][2] = {0};
	for (int i = 0; i < totalCmd + 1; i++) {
		cmdPipe[i][0] = -1;
		cmdPipe[i][1] = -1;
	}

	int i = 0, pid = 0;
	struct CmdNode *curent = head;
	pipe(cmdPipe[0]);
	while (curent) {
		char libPath[D_CMD_LEN] = {0};
		sprintf(libPath, "./%s/%s", path, curent->args[0]);

		if (strstr(curent->args[0], "printenv") != 0
				&& strstr(curent->args[1], "PATH") != 0) {
			char *s = getenv("PATH");
			printf("PATH=%s\n", s);
			fflush(stdout);
			close(cmdPipe[i][0]);
			close(cmdPipe[i][1]);
			cmdPipe[i][0] = -1;
			cmdPipe[i][1] = -1;
		} else if (strstr(curent->args[0], "setenv") != 0
				&& strstr(curent->args[1], "PATH") != 0) {
			setenv("PATH", curent->args[2], 1);
			sprintf(libPath, "./%s/%s", curent->args[2], curent->args[0]);
			strcpy(path, curent->args[2]);
			close(cmdPipe[i][0]);
			close(cmdPipe[i][1]);
			cmdPipe[i][0] = -1;
			cmdPipe[i][1] = -1;
		} else if (strstr(curent->cmd, "/") != 0) {
			perror("All arguments MUST NOT INCLUDE \"/\" for security.\n");
			memset(curent, '\0', sizeof(curent));
			close(cmdPipe[i][0]);
			close(cmdPipe[i][1]);
			cmdPipe[i][0] = -1;
			cmdPipe[i][1] = -1;
		} else {
			// Commend Pipe
			if (cmdPipe[i + 1][0] == -1 && cmdPipe[i + 1][1] == -1) {
				pipe(cmdPipe[i + 1]);
			}
			close(cmdPipe[i][1]);
			cmdPipe[i][1] = -1;

			// Line Pipe
			if (curent->type != CT_WRITE) {
				int jumpnum = curent->jumpto;
				if (jumpnum >= 0 && jumpnum < D_LINE_RANGE) {
					if (linePipe[jumpnum][0] == -1 && linePipe[jumpnum][1] == -1) {
						pipe(linePipe[jumpnum]);
					}
				}
			}
			close(linePipe[line][1]);
			linePipe[line][1] = -1;

			// Fork & Process Here
			pid = -1;
			while (pid == -1) {
				pid = fork();
			}
			if (pid > 0) {
				// Parent
				waitpid(pid, NULL, 0);
				if (i == 0) {
					close(linePipe[line][0]);
					linePipe[line][0] = -1;
				}
				close(cmdPipe[i][0]);
				cmdPipe[i][0] = -1;
			} else if (pid == 0) {
				// Child
				child_pipe_ctrl(curent, i, totalCmd, cmdPipe, line, linePipe, libPath);
			}
		}

		++i;
		curent = curent->next;
	}
	for (i = 0; i < (totalCmd + 1); i++) {
		if(cmdPipe[i][0] >= 0)
			close(cmdPipe[i][0]);
		if(cmdPipe[i][1] >= 0)
			close(cmdPipe[i][1]);
	}
}
