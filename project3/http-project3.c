#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>

#define SERV_TCP_PORT 8732
#define LISTEN_NUM 5

extern char **environ;

void exec_file(int fd);
void sig_chld(int);
int readline(int fd, char *ptr, int maxlen);

int main(int argc, char *argv[]) {
	int serv_sockfd, cli_sockfd, clilen, pid;
	struct sockaddr_in serv_addr, cli_addr;
	signal(SIGCHLD, sig_chld);

	/*
	 * Open a TCP socket (an Internet stream socket).
	 */
	if ((serv_sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		perror("server: can't open stream socket");
	/*
	 * Bind our local address so that the client can send to us.
	 */
	bzero((char*) &serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(SERV_TCP_PORT);
	if (bind(serv_sockfd, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) < 0)
		perror("server: can't bind local address");
	listen(serv_sockfd, LISTEN_NUM);

	while (1) {
		clilen = sizeof(cli_addr);
		cli_sockfd = accept(serv_sockfd, (struct sockaddr*) &cli_addr, (socklen_t*) &clilen);
		if (cli_sockfd < 0) {
			printf("Error: accept error.\n");
			return 0;
		}
		pid = fork();
		if (pid < 0) {
			printf("Error: fork failed.\n");
		} else if (pid == 0) {
			close(serv_sockfd);
			exec_file(cli_sockfd);
			exit(0);
		}
		close(cli_sockfd);
	}
	close(serv_sockfd);
	return 0;
}

void exec_file(int fd) {
	int pid;
	char buf[1000], file[1000], env[1000];

	memset(buf, '\0', sizeof(buf));
	readline(fd, buf, sizeof(buf));
	strtok(buf, "/");
	strcpy(file, strtok(NULL, " "));
	while (readline(fd, buf, sizeof(buf)) > 2)
		;

	pid = fork();
	if (pid == 0) {
		write(fd, "HTTP/1.1 200 OK\n", 16);
		dup2(fd, STDOUT_FILENO);
		//htm, html---------------------------------------------------
		if (strstr(file, ".htm") != NULL || strstr(file, ".html") != NULL) {
			printf("Content-type:text/html\n\n");
			fflush(NULL);
			if (access(file, F_OK) == 0) {
				execl("/bin/cat", "cat", file, NULL);
			} else {
				printf("Error: The File (%s) Not Exist.<br>\n", file);
				fflush(NULL);
			}
		}
		//cgi, else---------------------------------------------------
		else {
			// Remove Extra Env.
			unsetenv("HOME");
			unsetenv("SSH_CLIENT");
			unsetenv("HOST");
			unsetenv("SSH_CONNECTION");
			unsetenv("REMOTEHOST");
			unsetenv("BLOCKSIZE");
			unsetenv("OSTYPE");
			unsetenv("EDITOR");
			unsetenv("MAIL");
			unsetenv("PWD");
			unsetenv("VENDOR");
			unsetenv("USER");
			unsetenv("GROUP");
			unsetenv("LOGNAME");
			unsetenv("SHLVL");
			unsetenv("ENV");
			unsetenv("PATH");
			unsetenv("FTP_PASSIVE_MODE");
			unsetenv("MACHTYPE");
			unsetenv("SHELL");
			unsetenv("HOSTTYPE");
			unsetenv("TERM");
			unsetenv("PAGER");
			unsetenv("SSH_TTY");
			unsetenv("SCRIPT_NAME");
			unsetenv("NNTPSERVER");
			unsetenv("LESS");
			unsetenv("LSCOLORS");
			unsetenv("LANG");
			unsetenv("CLICOLOR");
			// Hold Env.
			if (strstr(file, "?") != NULL) {
				strcpy(file, strtok(file, "?"));
				strcpy(env, strtok(NULL, "\n"));
				setenv("QUERY_STRING", env, 1);
			}
			setenv("CONTENT_LENGTH", "5566", 1);
			setenv("REQUEST_METHOD", "GET", 1);
			setenv("SCRIPT_NAME", "/~icwu/chat/cgi-bin/echo-cgi", 1);
			setenv("REMOTE_HOST", "java.csie.nctu.edu.tw", 1);
			setenv("REMOTE_ADDR", "140.113.185.117", 1);
			setenv("ANTH_TYPE", "unknow", 1);
			setenv("REMOTE_USER", "0056110", 1);
			setenv("REMOTE_IDENT", "unknow", 1);

			if (access(file, F_OK) == 0) {
				execl(file, NULL);
			} else {
				printf(
						"Content-type:text/html\n\nError: The File (%s) Not Exist.<br>\n",
						file);
				fflush(NULL);
			}
		}
	}
}

int readline(int fd, char *ptr, int maxlen) {
	int n, rc;
	char c;
	for (n = 1; n < maxlen; n++) {
		if ((rc = read(fd, &c, 1)) == 1) {
			*ptr++ = c;
			if (c == '\n')
				break;
		} else if (rc == 0) {
			if (n == 1)
				return (0); /* EOF, no data read */
			else
				break; /* EOF, some data was read */
		} else
			return (-1); /* error */
	}
	*ptr = 0;
	return (n);
}

void sig_chld(int signo) {
	pid_t pid;
	int status;

	while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
	}
	return;
}

