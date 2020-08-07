#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>
#define SERV_TCP_PORT 9002
#define LISTEN_NUM 5

struct sockaddr_in cli_addr, dest_addr, sock_addr;

bool firewall(int cd) {
	FILE *fp;
	const char *mode;
	char argv[6][20];
	bool pass = false;

	if ((fp = fopen("socks.conf", "r")) == NULL) {
		printf("Error: socks.conf doesn't exist.\n");
		exit(0);
	}
	while (fscanf(fp, "%s %s %s %s %s %s\n", argv[0], argv[1], argv[2],
			argv[3], argv[4], argv[5]) != -1) {
		if (cd == 1) {
			mode = "c";
		} else if (cd == 2) {
			mode = "b";
		}

		if (strcmp(argv[1], mode) == 0) {
			if (strcmp(argv[0], "permit") == 0) {
				pass = true;
				if (strcmp(argv[2], "-") != 0
						&& strncmp((char*) inet_ntoa(cli_addr.sin_addr),
								argv[2], strlen(argv[2])) != 0)
					pass = false;
				if (strcmp(argv[3], "-") != 0
						&& atoi(argv[3]) != ntohs(cli_addr.sin_port))
					pass = false;
				if (strcmp(argv[4], "-") != 0
						&& strncmp((char*) inet_ntoa(dest_addr.sin_addr),
								argv[4], strlen(argv[4])) != 0)
					pass = false;
				if (strcmp(argv[5], "-") != 0
						&& atoi(argv[5]) != ntohs(dest_addr.sin_port))
					pass = false;
				if(pass)
					return true;
			} else if (strcmp(argv[0], "deny") == 0) {
				pass = true;
				if (strcmp(argv[2], "-") != 0
						&& strncmp((char*) inet_ntoa(cli_addr.sin_addr),
								argv[2], strlen(argv[2])) != 0)
					pass = false;
				if (strcmp(argv[3], "-") != 0
						&& atoi(argv[3]) != ntohs(cli_addr.sin_port))
					pass = false;
				if (strcmp(argv[4], "-") != 0
						&& strncmp((char*) inet_ntoa(dest_addr.sin_addr),
								argv[4], strlen(argv[4])) != 0)
					pass = false;
				if (strcmp(argv[5], "-") != 0
						&& atoi(argv[5]) != ntohs(dest_addr.sin_port))
					pass = false;
				if (!pass)
					return false;
			}
		}
	}

	return false;
}

void socks(int cli_fd) {
	int n, s_port, d_port, dest_fd;
	char *userid, vn, cd, S_IP[20], D_IP[20], request[32], reply[32],
			dest_url[1024];
	const char *command[] = { "CONNECT", "BIND" };
	struct hostent *dest_host;

	memset(request, '\0', sizeof(request));
	// Read socks4 request
	n = read(cli_fd, request, sizeof(request));

	vn = request[0];
	cd = request[1];
	memcpy(&dest_addr.sin_port, &request[2], 2);
	memcpy(&dest_addr.sin_addr.s_addr, &request[4], 4);
	userid = strtok(&request[8], " ");

	// Domain
	if (request[4] == 0 && request[5] == 0 && request[6] == 0
			&& request[7] != 0) {
		memset(&dest_url, '\0', sizeof(dest_url));
		sprintf(dest_url, "%s", &request[8 + strlen(&request[8]) + 1]);
		dest_host = gethostbyname(dest_url);
		dest_addr.sin_addr = *((struct in_addr*) dest_host->h_addr);
	}

	strcpy(S_IP, (char*) inet_ntoa(cli_addr.sin_addr));
	s_port = ntohs(cli_addr.sin_port);
	strcpy(D_IP, (char*) inet_ntoa(dest_addr.sin_addr));
	d_port = ntohs(dest_addr.sin_port);

	if (firewall(cd) == 0) {
		// SOCKS reply spec, cd=91 is reject
		reply[0] = 0;
		reply[1] = 91;
		write(cli_fd, reply, 8);
	} else {
		int my_port, socks_fd;

		if (cd == 1) {
			reply[0] = 0;
			reply[1] = 90;
			memcpy(&reply[2], &dest_addr.sin_port, 2);
			memcpy(&reply[4], &dest_addr.sin_addr.s_addr, 4);

			dest_addr.sin_family = AF_INET;
			if ((dest_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
				printf("Error: can't create socket\n");
				reply[1] = 91;
				write(cli_fd, reply, 8);
			}
			if (connect(dest_fd, (struct sockaddr*) &dest_addr,
					sizeof(dest_addr)) < 0) {
				printf("Error: can't connect to server.\n");
				reply[1] = 91;
				write(cli_fd, reply, 8);
			}
			write(cli_fd, reply, 8);
		} else if (cd == 2) //BIND
				{
			bzero((char*) &sock_addr, sizeof(sock_addr));
			srand((int) time(0));
			my_port = 9527 + (int) (rand() % 1000);
			sock_addr.sin_family = AF_INET;
			sock_addr.sin_addr.s_addr = INADDR_ANY;
			sock_addr.sin_port = htons(my_port);
			reply[0] = 0;
			reply[1] = 90;
			memcpy(&reply[2], &sock_addr.sin_port, 2);
			memcpy(&reply[4], &sock_addr.sin_addr.s_addr, 4);

			if ((socks_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
				printf("Error: can't create socket\n");
				reply[1] = 91;
				write(cli_fd, reply, 8);
			}
			if (bind(socks_fd, (struct sockaddr*) &sock_addr, sizeof(sock_addr))
					< 0) {
				printf("Error: can't bind address");
				reply[1] = 91;
				write(cli_fd, reply, 8);
			}
			listen(socks_fd, 20);
			write(cli_fd, reply, 8);

			memset(&dest_addr, 0, sizeof(dest_addr));
			socklen_t destlen = sizeof(dest_addr);
			dest_fd = accept(socks_fd, (struct sockaddr*) &dest_addr, &destlen);
			write(cli_fd, reply, 8);
		}

	}

	// SOCKS Server: Messages
	printf("=============\n");
	printf("<S_IP>   : %s\n", S_IP);
	printf("<S_PORT> : %d\n", s_port);
	printf("<D_IP>   : %s\n", D_IP);
	printf("<D_PORT> : %d\n", d_port);
	printf("<Command>: %s\n", command[cd - 1]);
	if (reply[1] == 90) {
		printf("<Reply>  : Accept\n");
	} else if (reply[1] == 91) {
		printf("<Reply>  : Reject\n");
		return;
	} else {
	}

	// Deliver
	int content = 0, nfds = getdtablesize();
	char buf[1024], *CmdTok;
	fd_set afds, rfds;
	FD_ZERO(&afds);
	FD_SET(cli_fd, &afds);
	FD_SET(dest_fd, &afds);

	while (1) {
		memcpy(&rfds, &afds, sizeof(rfds));
		if (select(nfds, &rfds, NULL, NULL, NULL) < 0) {
			printf("Error: select error.\n");
			exit(0);
		}

		if (FD_ISSET(dest_fd, &rfds)) {
			memset(buf, 0, sizeof(buf));
			n = read(dest_fd, buf, sizeof(buf));
			if (n == 0) {
				FD_CLR(cli_fd, &afds);
				FD_CLR(dest_fd, &afds);
				close(cli_fd);
				close(dest_fd);
				return;
			} else if(n > 0) {
				write(cli_fd, buf, n);
			}
		}
		if (FD_ISSET(cli_fd, &rfds)) {
			memset(buf, 0, sizeof(buf));
			n = read(cli_fd, buf, sizeof(buf));
			if (n == 0) {
				FD_CLR(cli_fd, &afds);
				FD_CLR(dest_fd, &afds);
				close(cli_fd);
				close(dest_fd);
				return;
			} else if(n > 0) {
				write(dest_fd, buf, n);
			}
		}

		if (content == 0) {
			CmdTok = strtok(buf, "\r\n");
			printf("<Content>: %s\n", CmdTok);
			content = 1;
		}
	}

}

void sig_chld(int signo) {
	pid_t pid;
	int status;
	while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
	}
	return;
}

int main(int argc, char *argv[]) {
	int msock, ssock, cli_len, pid;
	struct sockaddr_in serv_addr;
	signal(SIGCHLD, sig_chld);
	/*
	 * Open a TCP socket (an Internet stream socket).
	 */
	if ((msock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		perror("server: can't open stream socket");
	/*
	 * Bind our local address so that the client can send to us.
	 */
	int port = SERV_TCP_PORT;
	if(argc > 1) {
		port = atoi(argv[1]);
	}
	bzero((char*) &serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(port);
	while (bind(msock, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) < 0)
		perror("server: can't bind local address");
	listen(msock, LISTEN_NUM);

	while (1) {
		cli_len = sizeof(cli_addr);
		ssock = accept(msock, (struct sockaddr*) &cli_addr, (socklen_t*) &cli_len);
		if (ssock < 0)
			return 0;

		pid = fork();
		if (pid < 0)
			printf("error: system cannot fork\n");
		else if (pid == 0) {
			close(msock);
			socks(ssock);
			exit(0);
		} else
			close(ssock);
	}
	close(msock);
}
