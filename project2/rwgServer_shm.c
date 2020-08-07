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
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <pthread.h>

#define D_CMD_LEN 256
#define D_LINE_RANGE 1000
#define D_BUF_SIZE 4096
#define D_SERV_TCP_PORT 7000
#define D_LISTEN_NUM 30
#define D_SHM_KEY ((key_t) 6789)

enum CmdType { CT_NORMAL, CT_JUMP, CT_WRITE, CT_CHAT_PIPE};
enum PushType { PT_CLN, PT_OTHER, PT_ALL};

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
	char fifo[D_CMD_LEN];
	int ppid;
	int pid;
	char msgPipe[D_CMD_LEN];
};
struct ClientData *g_clnData;


void yell(char message[D_CMD_LEN], struct ClientData clnData[D_LISTEN_NUM + 1]);
void tell(char message[D_CMD_LEN], struct ClientData clnData[D_LISTEN_NUM + 1], int toId);
void trigger_broadcast(int id, char msg[]);
void strip_extra_spaces(char*);
int child_cut_cmd(struct CmdNode*, int);
void exec_cmd(char * const *, char * const *);
void op_pipe(int **, int, int **, int, bool);
bool exec_standalone_cmd(int id, struct ClientData clnData[D_LISTEN_NUM + 1], int linePipe[D_LINE_RANGE][2], struct CmdNode *curent, int cmdPipe[][2], int curCmd, char libPath[2 * D_CMD_LEN]);
void child_pipe_ctrl(int id, struct ClientData clnData[D_LISTEN_NUM + 1], struct CmdNode *, int, int, int** cmdPipe, int, int linePipe[D_LINE_RANGE][2], const char *);
void child_main(int id);
void child_process(int id, struct ClientData clnData[D_LISTEN_NUM + 1], struct CmdNode *, int, int linePipe[D_LINE_RANGE][2]);


void handle_pipe(int sig) {

}

int main(int argc, char *argv[], char *envp[]) {

	struct sigaction sginal_action;
	memset(&sginal_action, 0, sizeof (sginal_action));
	sginal_action.sa_handler = SIG_IGN;
	sginal_action.sa_flags = SA_RESTART;
	if (sigaction(SIGPIPE, &sginal_action, NULL)) {
	    perror("signal action error");
	}


	int servSockfd, newSockfd, childpid;
	struct sockaddr_in serv_addr;
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

	g_clnData = (struct ClientData*)mmap(NULL, sizeof(struct ClientData) * (D_LISTEN_NUM + 1), PROT_READ | PROT_WRITE,
            MAP_SHARED | MAP_ANONYMOUS, -1, 0);

	//int shmid = 0;
	//if ((shmid = shmget(D_SHM_KEY, (D_LISTEN_NUM + 1) * sizeof(struct ClientData),
	//		0666 | IPC_CREAT)) < 0)
	//	perror("share memory error!\n");
	//g_clnData = (struct ClientData *)shmat(shmid, NULL, 0);

	for (int i = 0; i <= D_LISTEN_NUM; i++) {
		g_clnData[i].sockfd = -1;
		g_clnData[i].port = -1;
		g_clnData[i].ip = NULL;
		g_clnData[i].line = 0;
		memset(g_clnData[i].libPath, '\0', sizeof(g_clnData[i].libPath));
		memset(g_clnData[i].name, '\0', sizeof(g_clnData[i].name));
		memset(g_clnData[i].fifo, '\0', sizeof(g_clnData[i].fifo));
	}


	int parentPid = getpid();
	while(1) {
		struct sockaddr_in cli_addr;
		int clilen = sizeof(cli_addr);
		newSockfd = accept(servSockfd, (struct sockaddr*) &cli_addr, (socklen_t*) &clilen);
		if (newSockfd < 0)
			perror("server: accept error");
		if ((childpid = fork()) < 0)
			perror("server: fork error");
		else if (childpid == 0) { /* child process */
			int i = 1;
			for (i = 1; i <= D_LISTEN_NUM && g_clnData[i].sockfd != -1; i++);
			g_clnData[i].sockfd = newSockfd;
			g_clnData[i].port = ntohs(cli_addr.sin_port);
			g_clnData[i].ip = inet_ntoa(cli_addr.sin_addr);
			g_clnData[i].line = 0;
			g_clnData[i].ppid = parentPid;
			strcpy(g_clnData[i].name,"(no name)");
			strcpy(g_clnData[i].libPath,"bin:.");
			sprintf(g_clnData[i].msgPipe, "/tmp/msgpipe%d", i);
			/* close original socket */
			close(servSockfd);
			/* process the request */
			child_main(i);
			exit(0);
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

void send_daemon(int id, struct ClientData g_clnData[D_LISTEN_NUM + 1]) {
	int pid = fork();
	if(pid == 0) {
		int readfd = open(g_clnData[id].msgPipe, O_RDONLY);
		while(1) {
			char buf[1024] = {0};
			int len = read(readfd, buf, sizeof(buf));
			if (len > 0) {
				write(g_clnData[id].sockfd, buf, len);
			} else {
				if(g_clnData[id].sockfd <= 0)
					break;
			}
		}

		exit(0);
	}
}

void push_msg(int id, struct ClientData g_clnData[D_LISTEN_NUM + 1], const char msg[2 * D_CMD_LEN], PushType type) {
	char tmsg[2 * D_CMD_LEN] = {0};
	strcpy(tmsg, msg);

	if(type == PT_CLN) {
		int pid = fork();
		if(pid == 0) {
			int writefd = open(g_clnData[id].msgPipe, O_WRONLY | O_CREAT);
			write(writefd, tmsg, strlen(tmsg));
			exit(0);
		}
	} else {
		for(int i = 1; i <= D_LISTEN_NUM; i++) {
			if(type == PT_OTHER && i == id) {
				continue;
			}

			if(g_clnData[i].sockfd > 0) {
				int pid = fork();
				if(pid == 0) {
					int writefd = open(g_clnData[i].msgPipe, O_WRONLY | O_CREAT);
					write(writefd, tmsg, strlen(tmsg));
					exit(0);
				}
			}
		}
	}
}

void child_main(int id) {

	int stdinOriFd = dup(STDIN_FILENO);
	int stdoutOriFd = dup(STDOUT_FILENO);
	int stderrOriFd = dup(STDERR_FILENO);
	int sockfd = g_clnData[id].sockfd;
	dup2(sockfd, STDIN_FILENO);
	dup2(sockfd, STDOUT_FILENO);
	dup2(sockfd, STDERR_FILENO);
	fflush (stdout);
	fflush (stderr);
	setenv("PATH", "bin:.", 1);


	unlink(g_clnData[id].msgPipe);
	mkfifo(g_clnData[id].msgPipe, S_IFIFO | 0666);
	printf("****************************************\n"
		   "** Welcome to the information server. **\n"
		   "****************************************\n");
	fflush(stdout);
	char msg[D_CMD_LEN] = {0};
	sprintf(msg, "*** User '(no name)' entered from %s/%d. ***\n",
			g_clnData[id].ip, g_clnData[id].port);
	push_msg(id, g_clnData, msg, PT_OTHER);
	printf("%s", msg);


	send_daemon(id, g_clnData);

	struct CmdNode *head = NULL;
	struct CmdNode *newCmd = NULL;
	struct CmdNode *prev = NULL;

	int n = 0;
	char ch = '~', preCh = '~';
	char temp[D_CMD_LEN] = {0};

	int linePipe[D_LINE_RANGE][2];
	for (int i = 0; i < D_LINE_RANGE; i++) {
		linePipe[i][0] = -1;
		linePipe[i][1] = -1;
	}

	write(sockfd, "% ", 2);
	dup2(stdoutOriFd, STDOUT_FILENO);
	printf("ClientId[%d] -> ", id);
	while(1) {
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
				if (strstr(newCmd->cmd, "exit") != 0) {
					for (int i = 0; i < D_LINE_RANGE; i++) {
						close(linePipe[i][0]);
						close(linePipe[i][1]);
					}

					int bk = g_clnData[id].sockfd;
					g_clnData[id].sockfd = -1;
					int eid = fork();
					if(eid == 0) {
						char msg[D_CMD_LEN] = {0};
						sprintf(msg, "*** User '%s' left. ***\n", g_clnData[id].name);
						push_msg(id, g_clnData, msg, PT_OTHER);
						exit(0);
					}
					waitpid(eid, NULL, 0);
					shutdown(bk, 2);
					close(bk);
					exit(0);
				}
				if (head) {
					dup2(sockfd, STDOUT_FILENO);
					// Parsing here
					int totalCmd = child_cut_cmd(head, g_clnData[id].line);
					// Fork & Pipe here
					setenv("PATH", g_clnData[id].libPath, 1);
					child_process(id, g_clnData, head, totalCmd, linePipe);

					// New Line
					head = NULL;
					newCmd = NULL;
					prev = NULL;
					g_clnData[id].line = (++g_clnData[id].line) % D_LINE_RANGE;
				}

				write(sockfd, "% ", 2);
				dup2(stdoutOriFd, STDOUT_FILENO);
				printf("ClientId[%d] -> ", id);
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

void child_pipe_ctrl(int id, struct ClientData clnData[D_LISTEN_NUM + 1], struct CmdNode *curent, int no, int totalCmd, int cmdPipe[][2], int linePipe[D_LINE_RANGE][2], const char *libPath) {
	int writefd;
	int line = clnData[id].line;
	int jumpnum = curent->jumpto;
	int fromChatPipe = curent->fromChatPipe;

	if (no == 0) {
		if (curent->type == CT_JUMP) {
			op_pipe(NULL, -1, linePipe, jumpnum, curent->piperr);
			if(linePipe[line][0] != -1) {
				op_pipe(linePipe, line, NULL, -1, curent->piperr);
			}
			if (fromChatPipe > 0) {
				if(strlen(clnData[fromChatPipe].fifo) > 0) {
					int readfd = open(clnData[fromChatPipe].fifo, O_RDONLY | O_NONBLOCK);
					dup2(readfd, STDIN_FILENO);
					close(readfd);
				} else {
					printf("*** Error: the pipe from #%d does not exist yet. ***\n", fromChatPipe);
					free(curent);
					exit(0);
				}
			}
			exec_cmd(clnData[id].sockfd, libPath, curent->args);
		} else if (curent->type == CT_NORMAL) {
			if(linePipe[line][0] != -1) {
				op_pipe(linePipe, line, NULL, -1, curent->piperr);
			}
			if (no < totalCmd - 1) {
				op_pipe(NULL, -1, cmdPipe, no + 1, curent->piperr);
			}
			if (fromChatPipe > 0) {
				if(strlen(clnData[fromChatPipe].fifo) > 0) {
					int readfd = open(clnData[fromChatPipe].fifo, O_RDONLY | O_NONBLOCK);
					dup2(readfd, STDIN_FILENO);
					close(readfd);

					char cmd[D_CMD_LEN] = {0};
					for (int i = 0; curent->args[i] != NULL; i++) {
						strcat(cmd, curent->args[i]);
						if (curent->args[i + 1] != NULL) {
							strcat(cmd, " ");
						}
					}
					char msg[D_CMD_LEN] = {0};
					sprintf(msg, "*** %s (#%d) just received the pipe from %s (#%d) by '%s <%d' ***\n",
							clnData[id].name, id, clnData[fromChatPipe].name, fromChatPipe, cmd, fromChatPipe);
					push_msg(id, clnData, msg, PT_OTHER);
					printf("%s", msg);

					memset(clnData[fromChatPipe].fifo, '\0', sizeof(clnData[fromChatPipe].fifo));
				} else {
					printf("*** Error: the pipe from #%d does not exist yet. ***\n", fromChatPipe);
					free(curent);
					exit(0);
				}
			}
			exec_cmd(clnData[id].sockfd, libPath, curent->args);
		} else if (curent->type == CT_WRITE) {
			writefd = open(curent->file, O_WRONLY | O_CREAT, 0666);
			dup2(writefd, STDOUT_FILENO);
			if (linePipe[line][0] != -1) {
				op_pipe(linePipe, line, NULL, -1, curent->piperr);
			}
			if (fromChatPipe > 0) {
				if(strlen(clnData[fromChatPipe].fifo) > 0) {
					int readfd = open(clnData[fromChatPipe].fifo, O_RDONLY | O_NONBLOCK);
					dup2(readfd, STDIN_FILENO);
					close(readfd);
				} else {
					printf("*** Error: the pipe from #%d does not exist yet. ***\n", fromChatPipe);
					free(curent);
					exit(0);
				}
			}
			exec_cmd(clnData[id].sockfd, libPath, curent->args);
		} else if (curent->type == CT_CHAT_PIPE) {
			char cmd[D_CMD_LEN] = {0};
			for (int i = 0; curent->args[i] != NULL; i++) {
				strcat(cmd, curent->args[i]);
				if (curent->args[i + 1] != NULL) {
					strcat(cmd, " ");
				}
			}

			if(strlen(clnData[id].fifo) == 0 || fromChatPipe == id) {
				int pid = fork();
				if (pid == 0) {
					//disable_SIGPIPE();
					if(linePipe[line][0] != -1) {
						op_pipe(linePipe, line, NULL, -1, curent->piperr);
					}
					if (fromChatPipe > 0) {
						if(strlen(clnData[fromChatPipe].fifo) > 0) {
							int readfd = open(clnData[fromChatPipe].fifo, O_RDONLY | O_NONBLOCK);
							dup2(readfd, STDIN_FILENO);
							close(readfd);
						} else {
							free(curent);
							exit(0);
						}
					}

					sprintf(clnData[id].fifo, "/tmp/fifo%d", id);
					unlink(clnData[id].fifo);
					mkfifo(clnData[id].fifo, S_IFIFO | 0666);
					writefd = open(clnData[id].fifo, O_WRONLY);
					if (curent->piperr == true) {
						dup2(writefd, STDERR_FILENO);
					}
					dup2(writefd, STDOUT_FILENO);
					close(writefd);
					exec_cmd(clnData[id].sockfd, libPath, curent->args);
				} else {
					char msg[2 * D_CMD_LEN] = {0};
					if (fromChatPipe > 0) {
						if(strlen(clnData[fromChatPipe].fifo) > 0) {
							sprintf(msg, "*** %s (#%d) just received the pipe from %s (#%d) by '%s <%d >|' ***\n",
									clnData[id].name, id, clnData[fromChatPipe].name, fromChatPipe, cmd, fromChatPipe);
						} else {
							printf("*** Error: the pipe from #%d does not exist yet. ***\n", fromChatPipe);
							free(curent);
							exit(0);
						}
					}

					if(fromChatPipe > 0) {
						sprintf(msg, "%s*** %s (#%d) just piped '%s <%d >|' into his/her pipe. ***\n", msg, clnData[id].name, id, cmd, fromChatPipe);
					} else {
						sprintf(msg, "%s*** %s (#%d) just piped '%s >|' into his/her pipe. ***\n", msg, clnData[id].name, id, cmd);
					}
					push_msg(id, clnData, msg, PT_OTHER);
					printf("%s", msg);
				}
				free(curent);
				exit(0);
			} else {
				printf("*** Error: your pipe already exists. ***\n");
				free(curent);
				exit(0);
			}
		}
	} else if (no < totalCmd - 1) {
		op_pipe(cmdPipe, no, cmdPipe, no + 1, curent->piperr);
		exec_cmd(clnData[id].sockfd, libPath, curent->args);
	} else { // (no == totalCmd - 1)
		if (curent->type == CT_JUMP) {
			op_pipe(NULL, -1, linePipe, jumpnum, curent->piperr);
			if(cmdPipe[no][0] != -1) {
				op_pipe(cmdPipe, no, NULL, -1, curent->piperr);
			}
			if (curent->fromChatPipe > 0) {
				if(strlen(clnData[fromChatPipe].fifo) > 0) {
					int readfd = open(clnData[fromChatPipe].fifo, O_RDONLY | O_NONBLOCK);
					dup2(readfd, STDIN_FILENO);
					close(readfd);
				} else {
					printf("*** Error: the pipe from #%d does not exist yet. ***\n", fromChatPipe);
					free(curent);
					exit(0);
				}
			}
			exec_cmd(clnData[id].sockfd, libPath, curent->args);
		} else if (curent->type == CT_NORMAL) {
			if (cmdPipe[no][0] != -1) {
				op_pipe(cmdPipe, no, NULL, -1, curent->piperr);
			}
			if (curent->fromChatPipe > 0) {
				if(strlen(clnData[fromChatPipe].fifo) > 0) {
					int readfd = open(clnData[fromChatPipe].fifo, O_RDONLY | O_NONBLOCK);
					dup2(readfd, STDIN_FILENO);
					close(readfd);
				} else {
					printf("*** Error: the pipe from #%d does not exist yet. ***\n", fromChatPipe);
					free(curent);
					exit(0);
				}
			}
			exec_cmd(clnData[id].sockfd, libPath, curent->args);
		} else if (curent->type == CT_WRITE) {
			if (cmdPipe[no][0] != -1) {
				op_pipe(cmdPipe, no, NULL, -1, curent->piperr);
			}
			if (curent->fromChatPipe > 0) {
				if(strlen(clnData[fromChatPipe].fifo) > 0) {
					int readfd = open(clnData[fromChatPipe].fifo, O_RDONLY | O_NONBLOCK);
					dup2(readfd, STDIN_FILENO);
					close(readfd);
				} else {
					printf("*** Error: the pipe from #%d does not exist yet. ***\n", fromChatPipe);
					free(curent);
					exit(0);
				}
			}
			writefd = open(curent->file, O_WRONLY | O_CREAT, 0666);
			dup2(writefd, STDOUT_FILENO);
			exec_cmd(clnData[id].sockfd, libPath, curent->args);
		} else if (curent->type == CT_CHAT_PIPE) {
			int pid = fork();
			if (pid == 0) {
				//disable_SIGPIPE();
				if (curent->fromChatPipe > 0) {
					if(strlen(clnData[fromChatPipe].fifo) > 0) {
						int readfd = open(clnData[fromChatPipe].fifo, O_RDONLY | O_NONBLOCK);
						dup2(readfd, STDIN_FILENO);
						close(readfd);
					} else {
						printf("*** Error: the pipe from #%d does not exist yet. ***\n", fromChatPipe);
						free(curent);
						exit(0);
					}
				}
				if(cmdPipe[no][0] != -1) {
					op_pipe(cmdPipe, no, NULL, -1, curent->piperr);
				}
				exec_cmd(clnData[id].sockfd, libPath, curent->args);
			} else {
				char cmd[D_CMD_LEN] = {0};
				for (int i = 0; curent->args[i] != NULL; i++) {
					strcat(cmd, curent->args[i]);
					if (curent->args[i + 1] != NULL) {
						strcat(cmd, " ");
					}
				}
				char msg[D_CMD_LEN] = {0};
				sprintf(msg, "*** %s (#%d) just piped '%s >|' into his/her pipe. ***\n", clnData[id].name, id, cmd);
				push_msg(id, clnData, msg, PT_OTHER);
				printf("%s", msg);
			}
		}
	}

	if (fromChatPipe > 0) {
		if(strlen(clnData[fromChatPipe].fifo) > 0) {
			memset(clnData[fromChatPipe].fifo, '\0', sizeof(clnData[fromChatPipe].fifo));
		}
	}

	free(curent);
	exit(0);
}

bool exec_standalone_cmd(int id, struct ClientData clnData[D_LISTEN_NUM + 1], int linePipe[D_LINE_RANGE][2], struct CmdNode *curent, int cmdPipe[][2], int curCmd, char libPath[2 * D_CMD_LEN]) {
	bool hit = false;
	if (strstr(curent->args[0], "printenv") != 0
			&& strstr(curent->args[1], "PATH") != 0) {
		char *s = getenv("PATH");
		printf("PATH=%s\n", s);
		fflush(stdout);
		close(cmdPipe[curCmd][0]);
		close(cmdPipe[curCmd][1]);
		cmdPipe[curCmd][0] = -1;
		cmdPipe[curCmd][1] = -1;
		hit = true;
	} else if (strstr(curent->args[0], "setenv") != 0
			&& strstr(curent->args[1], "PATH") != 0) {
		setenv("PATH", curent->args[2], 1);
		sprintf(libPath, "./%s/%s", curent->args[2], curent->args[0]);
		strcpy(clnData[id].libPath, curent->args[2]);
		close(cmdPipe[curCmd][0]);
		close(cmdPipe[curCmd][1]);
		cmdPipe[curCmd][0] = -1;
		cmdPipe[curCmd][1] = -1;
		hit = true;
	} else if (strstr(curent->cmd, "/") != 0) {
		perror("All arguments MUST NOT INCLUDE \"/\" for security.\n");
		memset(curent, '\0', sizeof(curent));
		close(cmdPipe[curCmd][0]);
		close(cmdPipe[curCmd][1]);
		cmdPipe[curCmd][0] = -1;
		cmdPipe[curCmd][1] = -1;
		hit = true;
	} else if (strstr(curent->args[0], "name") != 0) {
		strcpy(clnData[id].name, curent->args[1]);
		char msg[D_CMD_LEN] = {0};
		sprintf(msg, "*** User from %s/%d is named '%s'. ***\n", clnData[id].ip, clnData[id].port, clnData[id].name);
		push_msg(id, clnData, msg, PT_OTHER);
		printf("%s", msg);
		hit = true;
	} else if (strstr(curent->args[0], "who") != 0) {
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
	} else if (strstr(curent->args[0], "tell") != 0) {
		if(clnData[atoi(curent->args[1])].sockfd <= 0 || atoi(curent->args[1]) > D_LISTEN_NUM) {
			printf("*** Error: user #%s does not exist yet. ***\n", curent->args[1]);
		} else {
			char tmp[D_CMD_LEN] = {0};
			for (int i = 2; curent->args[i] != NULL; i++) {
				strcat(tmp, curent->args[i]);
				if (curent->args[i + 1] != NULL) {
					strcat(tmp, " ");
				}
			}
			char msg[D_CMD_LEN] = {0};
			sprintf(msg, "*** %s told you ***:  %s\n", clnData[id].name, tmp);
			push_msg(atoi(curent->args[1]), clnData, msg, PT_CLN);
		}
		hit = true;
	} else if (strstr(curent->args[0], "yell") != 0) {
		char tmp[D_CMD_LEN] = {0};
		for (int i = 1; curent->args[i] != NULL; i++) {
			strcat(tmp, curent->args[i]);
			if (curent->args[i + 1] != NULL) {
				strcat(tmp, " ");
			}
		}

		char msg[D_CMD_LEN] = {0};
		sprintf(msg, "*** %s yelled ***: %s\n", clnData[id].name, tmp);
		push_msg(id, clnData, msg, PT_OTHER);
		printf("%s", msg);
		hit = true;
	}

	return hit;
}

void child_process(int id, struct ClientData clnData[D_LISTEN_NUM + 1], struct CmdNode *head, int totalCmd, int linePipe[D_LINE_RANGE][2]) {

	int line = clnData[id].line;

	// 2-dim pipe array
	int cmdPipe[totalCmd + 1][2] = {0};
	for (int i = 0; i < totalCmd + 1; i++) {
		cmdPipe[i][0] = -1;
		cmdPipe[i][1] = -1;
	}
	pipe(cmdPipe[0]);

	int i = 0, pid = 0;
	struct CmdNode *curent = head;
	while (curent) {
		char libPath[2 * D_CMD_LEN] = {0};
		if(strstr(clnData[id].libPath, "bin") != NULL) {
			sprintf(libPath, "./bin/%s", curent->args[0]);
		} else {
			sprintf(libPath, "./%s/%s", clnData[id].libPath, curent->args[0]);
		}

		if(exec_standalone_cmd(id, clnData, linePipe, curent, cmdPipe, i, libPath)) {
			break;
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

				++i;
				curent = curent->next;
			} else if (pid == 0) {
				// Child
				child_pipe_ctrl(id, clnData, curent, i, totalCmd, cmdPipe, linePipe, libPath);
				exit(0);
			}
		}
	}
	for (i = 0; i < (totalCmd + 1); i++) {
		if(cmdPipe[i][0] >= 0)
			close(cmdPipe[i][0]);
		if(cmdPipe[i][1] >= 0)
			close(cmdPipe[i][1]);
	}
	fflush(stdout);
}
