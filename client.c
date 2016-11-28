/*
 * client.c
 *
 *  Created on: Not 23, 2016
 *      Author: RyanMini
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAXLINE     8192
#define RIO_BUFSIZE 8192

typedef struct {
	int rio_fd;                /* Descriptor for this internal buf */
	int rio_cnt;               /* Unread bytes in internal buf */
	char *rio_bufptr;          /* Next unread byte in internal buf */
	char rio_buf[RIO_BUFSIZE]; /* Internal buffer */
} rio_t;

typedef struct sockaddr SA;

void rio_readinitb(rio_t *rp, int fd)
{
    rp->rio_fd = fd;
    rp->rio_cnt = 0;
    rp->rio_bufptr = rp->rio_buf;
}

static ssize_t rio_read(rio_t *rp, char *usrbuf, size_t n)
{
    int cnt;

    while (rp->rio_cnt <= 0) {  /* refill if buf is empty */
	rp->rio_cnt = read(rp->rio_fd, rp->rio_buf,
			   sizeof(rp->rio_buf));
	if (rp->rio_cnt < 0) {
	    if (errno != EINTR) /* interrupted by sig handler return */
		return -1;
	}
	else if (rp->rio_cnt == 0)  /* EOF */
	    return 0;
	else
	    rp->rio_bufptr = rp->rio_buf; /* reset buffer ptr */
    }

    /* Copy min(n, rp->rio_cnt) bytes from internal buf to user buf */
    cnt = n;
    if (rp->rio_cnt < n)
	cnt = rp->rio_cnt;
    memcpy(usrbuf, rp->rio_bufptr, cnt);
    rp->rio_bufptr += cnt;
    rp->rio_cnt -= cnt;
    return cnt;
}

ssize_t rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen)
{
    int n, rc;
    char c, *bufp = usrbuf;

    for (n = 1; n < maxlen; n++) {
	if ((rc = rio_read(rp, &c, 1)) == 1) {
	    *bufp++ = c;
	    if (c == '\n')
		break;
	} else if (rc == 0) {
	    if (n == 1)
		return 0; /* EOF, no data read */
	    else
		break;    /* EOF, some data was read */
	} else
	    return -1;	  /* error */
    }
    *bufp = 0;
    return n;
}

int open_clientfd(char *hostname, int port){
	int clientfd;
	struct hostent *hp;
	struct sockaddr_in serveraddr;
	if ((clientfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
		return -1;
	}
	/* Check errno for cause of error */
	/* Fill in the server’s IP address and port */
	if ((hp = gethostbyname(hostname)) == NULL){
		return -2; /* Check h_errno for cause of error */
	}
	bzero((char *) &serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	bcopy((char *)hp->h_addr_list[0], (char *)&serveraddr.sin_addr.s_addr, hp->h_length);
	serveraddr.sin_port = htons(port);
	/* Establish a connection with the server */
	if (connect(clientfd, (SA *) &serveraddr, sizeof(serveraddr)) < 0){
		return -1;
	}
	return clientfd;
}

ssize_t rio_writen(int fd, void *usrbuf, size_t n)
{
    size_t nleft = n;
    ssize_t nwritten;
    char *bufp = usrbuf;

    while (nleft > 0) {
	if ((nwritten = write(fd, bufp, nleft)) <= 0) {
	    if (errno == EINTR)  /* interrupted by sig handler return */
		nwritten = 0;    /* and call write() again */
	    else
		return -1;       /* errorno set by write() */
	}
	nleft -= nwritten;
	bufp += nwritten;
    }
    return n;
}

/*******************************************************************************************************************/

int main(int argc, char ** argv){

	int clientfd, port;
	char * host, buf[MAXLINE];
	rio_t rio;

	if (argc != 3){
		fprintf(stderr, "usage:  %s <host> <port>\n", argv[0]);
		exit(0);
	}

	host = argv[1];
	port = atoi(argv[2]);

	clientfd = open_clientfd(host, port);

	printf("Connected to %s on port %d\n\n", host, port);


	char input[MAXLINE];
	char filepath[MAXLINE];

	rio_readinitb(&rio, clientfd);

	while (1){

		printf("Enter:  {OPEN}, {READ}, {WRITE}, {ECHO}, or {CLOSE}\n");
		scanf("%s", input);

	/*
		printf("Enter filepath:  ");
		scanf("%s", filepath);
		printf("\nOperation chosen:  %s\nFilepath given:  %s\n", input, filepath);
	*/

		if (!strcmp(input, "OPEN")){
			printf("Open not supported at this time...\n");
			// netopen();
		}
		else if (!strcmp(input, "READ")){
			printf("Read not supported at this time...\n");
			// netread();
		}
		else if (!strcmp(input, "WRITE")){
			printf("Write not supported at this time...\n");
			// netwrite();
		}
		else if (!strcmp(input, "ECHO")){
			while (fgets(buf, MAXLINE, stdin) != NULL){
				if (!strcmp(buf, "q\n")){
					printf("Quitting ECHO...\n");
					break;
				}
				rio_writen(clientfd, buf, strlen(buf));
				rio_readlineb(&rio, buf, MAXLINE);
			}
		}
		else if (!strcmp(input, "CLOSE")){
			// must tell server to release file descriptor, using netclose()
			// netclose();
			close(clientfd);
			break;
		}
		else {
			printf("Command not supported...\n");
			continue;
		}
	}


	printf("\nTerminating");
	fflush(stdout);
    int i;
    for (i = 0; i < 3; i++){
    	printf(".");
    	fflush(stdout);
    	sleep(1);
    }
    printf(" Done\n");


	exit(0);



}
