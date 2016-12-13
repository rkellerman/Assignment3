/*
 * client.c
 *
 *  Created on: Not 23, 2016
 *      Author: RyanMini
 */

//A simple coment to test git branching

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
#include "header.h"


#define MAXLINE     8192
#define RIO_BUFSIZE 8192

 #define UNRESTRICTED 0
 #define EXCLUSIVE    1
 #define TRANSACTION  2



typedef struct sockaddr SA;

/*

	Calledd once per open descriptor. Assiciates the descriptor fd with a read 
	buffer of type rio_t at address rp.
*/
void rio_readinitb(rio_t *rp, int fd)
{
    rp->rio_fd = fd;
    rp->rio_cnt = 0;
    rp->rio_bufptr = rp->rio_buf;
}


/*
	Reads up to n bytes from file descriptor rp and places it in the buffer

*/

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

/*
	Reads the next text line from file rp, copies it to memory location usrbuf,
	and termines the text line with the null (zero) character. The rio_readlineb
	function reads at most maxlen -1 bytes, leaving room for the terminating null character

*/

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
/*

	INPUT:
		hostname: The server to which a connection is established

	OUTPUT: Open socket descriptor that is ready for input and output using
	Unix I/O functions
	Establishes a connection with a server running on host "hostname" and 
	listens for connection requests on the well-known port "port"
*/
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

/*

	Transfer n bytes from location usrbuf to descripter fd
*/

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

int port, clientfd, filedesc;
int init = 0;   
char * host;
rio_t rio;
char file[10000];

int netopen(char * pathname, int flags){

	if (init == 0){
		printf("Please run init first\n");
		return -1;
	}

	char buf[MAXLINE];


	clientfd = open_clientfd(host, port);
	rio_readinitb(&rio, clientfd);


	// write file path and flags to server program
	// first write OPEN to buf and pass it

	sprintf(buf, "OPEN\n");

	rio_writen(clientfd, buf, strlen(buf));
	rio_readlineb(&rio, buf, MAXLINE);

	if (!strcmp(buf, "PROCEED\n")){
		//printf("I CAN PROCEED\n");
	}
	else {
		// report failure
	}

	// pathname does not need to be modified, write it to server

	int len = strlen(pathname);
	pathname[len] = '\n';
	rio_writen(clientfd, pathname, strlen(pathname));
	rio_readlineb(&rio, buf, MAXLINE);
	if (!strcmp(buf, "PROCEED\n")){
		//printf("I CAN PROCEED\n");
	}
	else {
		// report failure
	}

	// convert flags to string that is held by buf and write them to server

	sprintf(buf, "%d\n", flags);
	rio_writen(clientfd, buf, strlen(buf));
	rio_readlineb(&rio, buf, MAXLINE);
	if (!strcmp(buf, "PROCEED\n")){
		//printf("I CAN PROCEED\n");
	}
	else {
		// report failure
	}

	// receive response from program
	rio_readlineb(&rio, buf, MAXLINE);		// receive response in buf, modify to be a string for atoi
	buf[strlen(buf)-1] = '\0';
	if (!strcmp(buf, "WAIT")){

		printf("File request blocked due to policy conflict, please wait...\n");
		rio_readlineb(&rio, buf, MAXLINE);	
		buf[strlen(buf)-1] = '\0';

		if (!strcmp(buf, "TIMEOUT")){
			printf("Timeout error....\n");
			close(clientfd);
			return -1;
		}
	}
	printf("Client received: %s\n", buf);


	// buf holds desired file descriptor

	close(clientfd);

	return atoi(buf);
}

int netread(int fildes, char * buf, size_t nbyte){

	if (init == 0){
		printf("Please run init first\n");
		return -1;
	}


	char sub[MAXLINE];


	clientfd = open_clientfd(host, port);
	rio_readinitb(&rio, clientfd);

	sprintf(sub, "READ\n");

	rio_writen(clientfd, sub, strlen(sub));
	rio_readlineb(&rio, sub, MAXLINE);

	if (!strcmp(sub, "PROCEED\n")){
		//printf("I CAN PROCEED\n");
	}
	else {
		// report failure
	}

	sprintf(sub, "%d\n", fildes);
	rio_writen(clientfd, sub, strlen(sub));
	rio_readlineb(&rio, sub, MAXLINE);

	if (!strcmp(sub, "PROCEED\n")){
		//printf("I CAN PROCEED\n");
	}
	else {
		// report failure
	}

	char * charbyte = malloc(10);
	rio_readlineb(&rio, charbyte, MAXLINE);
	charbyte[strlen(charbyte)-1] = '\0';

	int numbytes = atoi(charbyte);

	//char * file = malloc(numbytes + 1);
	rio_readlineb(&rio, buf, MAXLINE);
	buf[strlen(buf) - 1] = '\0';

	close(clientfd);

	return numbytes;
}

int netwrite(int fildes, char * file, size_t size){

	if (init == 0){
		printf("Please run init first\n");
		return -1;
	}

	char sub[MAXLINE];


	clientfd = open_clientfd(host, port);
	rio_readinitb(&rio, clientfd);

	sprintf(sub, "WRITE\n");

	rio_writen(clientfd, sub, strlen(sub));
	rio_readlineb(&rio, sub, MAXLINE);

	if (!strcmp(sub, "PROCEED\n")){
		//printf("I CAN PROCEED\n");
	}
	else {
		// report failure
	}

	// write the file decriptor to the server
	sprintf(sub, "%d\n", fildes);
	rio_writen(clientfd, sub, strlen(sub));
	rio_readlineb(&rio, sub, MAXLINE);

	if (!strcmp(sub, "PROCEED\n")){
		//printf("I CAN PROCEED\n");
	}
	else {
		// report failure
	}


	// write text to server
	sprintf(file, "%s\n", file);
	printf("File to be written:  %s", file);
	rio_writen(clientfd, file, strlen(file));
	rio_readlineb(&rio, sub, MAXLINE);

	if (!strcmp(sub, "PROCEED\n")){
		//printf("I CAN PROCEED\n");
	}
	else {
		// report failure
	}

	rio_readlineb(&rio, sub, MAXLINE);
	sub[strlen(sub) - 1] = '\0';

	close(clientfd);

	return atoi(sub);

}

int netclose(int fildes){

	if (init == 0){
		printf("Please run init first\n");
		return -1;
	}

	char sub[MAXLINE];


	clientfd = open_clientfd(host, port);
	rio_readinitb(&rio, clientfd);

	sprintf(sub, "CLOSE\n");

	rio_writen(clientfd, sub, strlen(sub));
	rio_readlineb(&rio, sub, MAXLINE);

	if (!strcmp(sub, "PROCEED\n")){
		//printf("I CAN PROCEED\n");
	}
	else {
		// report failure
	}

	sprintf(sub, "%d\n", fildes);
	rio_writen(clientfd, sub, strlen(sub));
	rio_readlineb(&rio, sub, MAXLINE);

	if (!strcmp(sub, "PROCEED\n")){
		//printf("I CAN PROCEED\n");
	}
	else {
		// report failure
	}

	char * charbyte = malloc(10);
	rio_readlineb(&rio, charbyte, MAXLINE);
	charbyte[strlen(charbyte)-1] = '\0';

	close(clientfd);

	return atoi(charbyte);

}

int netserverinit(char * hostname, int filemode){

	errno = 0;

	struct hostent * hp;
	if ((hp = gethostbyname(hostname)) == NULL){
		errno = HOST_NOT_FOUND;
		return -1; /* Check h_errno for cause of error */
	}

	char buf[MAXLINE];


	clientfd = open_clientfd(host, port);
	rio_readinitb(&rio, clientfd);


	// write file path and flags to server program
	// first write OPEN to buf and pass it

	sprintf(buf, "INIT\n");


	rio_writen(clientfd, buf, strlen(buf));
	rio_readlineb(&rio, buf, MAXLINE);

	if (!strcmp(buf, "PROCEED\n")){
		//printf("I CAN PROCEED\n");
	}
	else {
		printf("Server is initialized....\n");
		init = 1;
		close(clientfd);
		return 0;     // technically not an error
	}

	sprintf(buf, "%d\n", filemode);

	rio_writen(clientfd, buf, strlen(buf));
	rio_readlineb(&rio, buf, MAXLINE);

	if (!strcmp(buf, "PROCEED\n")){
		//printf("I CAN PROCEED\n");
	}
	else {
		printf("Invalid filemode, use 0, 1, or 2\n");
		close(clientfd);
		return -1;
	}

	init = 1;
	printf("Init successful\n");
	close(clientfd);
	return 0;


}

int main(int argc, char ** argv){

	char buf[MAXLINE];

	if (argc != 3){
		fprintf(stderr, "usage:  %s <host> <port>\n", argv[0]);
		exit(0);
	}

	host = argv[1];
	port = atoi(argv[2]);

	char input[MAXLINE];
	char filepath[MAXLINE];

	while (1){

		printf("Enter:  {OPEN}, {READ}, {WRITE}, {ECHO}, {INIT}, or {CLOSE}\n");
		scanf("%s", input);


		if (!strcmp(input, "OPEN")){
			printf("Enter filepath:  ");
			scanf("%s", filepath);
			filedesc = netopen(filepath, O_RDONLY);
		}
		else if (!strcmp(input, "READ")){
			printf("Enter file descriptor:  ");
			char * charfile = malloc(3);
			scanf("%s", charfile);
			int filedesc = atoi(charfile);
			int numbytes = netread(filedesc, file, sizeof(file));

			printf("%d bytes read...  The following are the contents of the file....\n%s\n", numbytes, file);
		}
		else if (!strcmp(input, "INIT")){
			printf("Enter filemode {0}, {1}, or {2}\n");
			char * charfile = malloc(3);
			scanf("%s", charfile);
			int filemode = atoi(charfile);
			netserverinit(host, filemode);
		}
		else if (!strcmp(input, "WRITE")){
			printf("Enter file descriptor:  ");
			char * charfile = malloc(3);
			scanf("%s", charfile);
			int filedesc = atoi(charfile);
			printf("Enter text to be written:  ");



			char *text = calloc(1,1);
			int i = 0;
			while( fgets(file, 10000, stdin) ) /* break with ^D or ^Z */
			{
				text = realloc( text, strlen(text)+1+strlen(file) );
				if( !text ){} /* error handling */
				strcat( text, file ); /* note a '\n' is appended here everytime */
				printf("%s\n", file);
				i++;
				if (i == 2){
					break;
				}
			}
			printf("\ntext:\n%s",text);
			int success = netwrite(filedesc, file, strlen(file));
			printf("Number of bytes written is %d\n", success);
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
			printf("Enter file descriptor:  ");
			char * charfile = malloc(3);
			scanf("%s", charfile);
			int filedesc = atoi(charfile);
			int success = netclose(filedesc);
			printf("Success:  %d\n", success);
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
