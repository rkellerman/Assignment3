/*
 * server.c
 *
 *  Created on: Nov 23, 2016
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

#define NTHREADS 4
#define SBUFSIZE 16
#define LISTENQ  1024
#define MAXLINE     8192
#define RIO_BUFSIZE 8192

typedef struct {
	int * buf; 				// buffer array
	int n; 					// maximum number of slots
	int front; 				// buf[(front + 1) % n] is the first item
	int rear; 				// buf[read % n] is the last item
	sem_t mutex; 			// protects access to buf
	sem_t slots; 			// counts available slots
	sem_t items; 			// counts avaiable items
} sbuf_t;

int itemsVal = 0;
int slotsVal;

int unrestricted;
pthread_t exclusive;
pthread_t transaction;


typedef struct {
	int rio_fd;                /* Descriptor for this internal buf */
	int rio_cnt;               /* Unread bytes in internal buf */
	char *rio_bufptr;          /* Next unread byte in internal buf */
	char rio_buf[RIO_BUFSIZE]; /* Internal buffer */
} rio_t;

typedef struct sockaddr SA;

typedef struct{
	char * filename;
	int mode;
	int read;
	int write;
	struct Metadata * next;
}  Metadata;


/*
	Semaphore functions
*/

void P(sem_t *sem)
{
    if (sem_wait(sem) < 0){
    	printf("P error\n");
    	exit(0);
    }
}

void V(sem_t *sem)
{
    if (sem_post(sem) < 0){
    	printf("V error\n");
    	exit(0);
    }
}

/*
	Create an empty, bounded, shared FIFO buffer with n slots
*/

void sbuf_init(sbuf_t * sp, int n){
	sp->buf = malloc(n*sizeof(int));
	sp->n = n;									// buffer holds max of n items
	sp->front = sp->rear = 0;					// initally empty buffer where front == rear
	slotsVal = n;

/*
	sp->mutex = sem_open("/mutex", O_CREAT, 0644, 1);
	sp->slots = sem_open("/slots", O_CREAT, 0644, n);
	sp->items = sem_open("/items", O_CREAT, 0644, 0);
*/



	sem_init(&sp->mutex, 0, 1);					// binary semaphore for locking
	sem_init(&sp->slots, 0, n); 				// initally has n empty slots
	sem_init(&sp->items, 0, 0);					// initally holds 0 data items


	printf("Init complete\n");

}

/*
	Insert intem onto the rear of shared buffer sp
*/

void sbuf_insert(sbuf_t * sp, int item){

	sem_wait(&sp->slots);						// wait until available slot
	sem_wait(&sp->mutex); 						// when spot becomes open, lock access to buffer
	sp->buf[(++sp->rear) % (sp->n)] = item; 	// insert item
	slotsVal--;
	itemsVal++;
	printf("Insert complete, slots at %d, items at %d\n", slotsVal, itemsVal);
	sem_post(&sp->mutex); 						// unlock the buffer
	sem_post(&sp->items); 						// announce that an item is available

}
/*
	Remove and return the first item from buffer sp
*/
int sbuf_remove(sbuf_t * sp){

	int item;
	sem_wait(&sp->items); 						// wait until available item
	sem_wait(&sp->mutex); 						// when item is available, lock access to the buffer
	item = sp->buf[(++sp->front) % (sp->n)]; 	// remove the item
	slotsVal++;
	itemsVal--;
	printf("Remove complete, slots at %d, items at %d\n", slotsVal, itemsVal);
	sem_post(&sp->mutex);						// unlock the buffer
	sem_post(&sp->slots); 						// announce that a slot is available

	return item;
}

/*
	Opens and returns a litening descriptor that is ready to receive connection requests
	on a well-known port port
*/

int open_listenfd(int port){
	int listenfd, optval = 1;
	struct sockaddr_in serveraddr;

	// Create a socket descriptor
	if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
		return -1;
	}

	// Eliminates an "Address already in use" error from bind
	if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR,(const void *)&optval , sizeof(int)) < 0){
		return -1;
	}

	// Listenfd will be an end point for all request to port on any IP address for this host
	bzero((char *) &serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serveraddr.sin_port = htons((unsigned short)port);
	if (bind(listenfd, (SA *)&serveraddr, sizeof(serveraddr)) < 0){
		return -1;
	}

	/* Make it a listening socket ready to accept connection requests */
	if (listen(listenfd, LISTENQ) < 0){
		return -1;
	}

	return listenfd;

}

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

sbuf_t sbuf; 									// shared buffer of connected descriptors
rio_t rio;
Metadata * head;


 Metadata * searchList(char * filename){
	Metadata *pointer;
	pointer = (Metadata *) malloc(sizeof(Metadata));

	for (pointer = head; pointer != NULL; pointer = (Metadata*) pointer->next){
		if (strcmp(pointer->filename, filename) != 0){
			return pointer;
		}

	}

	return pointer;

}





void work_open(int connfd){ 			// be sure to use semaphores
	Metadata * data;
	char *  access;
	printf("OPEN FUNCTION RUNNING...\n");
	
	char buf[MAXLINE];
	
	rio_readlineb(&rio, buf, MAXLINE);
   char * pathname = malloc(strlen(buf));
   strcpy(pathname, buf);
   pathname[strlen(buf)-1] = '\0';
   
   //Here is where we search the linked list
	data = searchList(pathname);	//Gets the struct that holds the right metadata
	


	sprintf(buf, "PROCEED\n");
   rio_writen(connfd, buf, strlen(buf));	
   
   //Finds which flags are preesent
   char * flags = malloc(2);
   rio_readlineb(&rio, flags, MAXLINE);
   flags[1] = '\0';
   //At this point, flags have been obtained
   
   sprintf(buf, "PROCEED\n");
   rio_writen(connfd, buf, strlen(buf));
   
   printf("Pathname is \"%s\"\n", pathname);
   
   int flag = atoi(flags);
   int filedesc = open(pathname, flag);
   
   //Gets the accesstype
   rio_readlineb(&rio, access, MAXLINE);
   int accessType = atoi(access);
   //Create a new node in the linked list
	if (data == NULL){
		Metadata * newNode = (Metadata*) malloc(sizeof(Metadata));
		newNode->filename = pathname;
		newNode->mode = accessType;
		newNode->next = head;
	}
	else{
		int modeBit = data->mode;
		//Transition mode: If transition mode is requested and no other permissions issued
		if ((accessType == 2)  && (modeBit == -1)){
			printf("TRANSACTION MODE access granted\n");
			data->mode = accessType;
		}
		/*Exclusive mode: Exclusive requested and no other permissions granted OR in unrestricted
		mode with no writers*/
		else if ((accessType == 1 && modeBit == -1) || (accessType == 1 && modeBit == 0 && data->write ==0)){
			printf("EXCLUSIVE MODE access granted\n");
			data->mode = accessType;
			data->write++;
		}
		//Unrestricted: Only if not in other
		else if ((accessType == 0 && modeBit != 2) || (accessType == 0 && modeBit == 1  && flag == O_RDONLY)){
			printf("UNRESTRICTED MODE access granted\n");
			data->mode = 0;
			data->write++;
		}
		else{
			printf("Permission Denied\n");
			return;

		}

	}
   printf("Flag is %d, length of pathname is %d\n", flag, strlen(pathname));
   printf("File descriptor created: %d\n", filedesc);
   
   char * charfile = malloc(3);
   sprintf(charfile, "%d\n", filedesc);
   rio_writen(connfd, charfile, strlen(charfile));
   
   printf("ending function\n");
   //freeing malloc-ed items
   free(flags);
   free(charfile);
   return;


}

void work_read(int connfd){

   char buf[MAXLINE];
   rio_readlineb(&rio, buf, MAXLINE);
   char * charfile = malloc(strlen(buf));
   strcpy(charfile, buf);
   charfile[strlen(buf)-1] = '\0';
   
   sprintf(buf, "PROCEED\n");
	rio_writen(connfd, buf, strlen(buf));
   
   int filedesc = atoi(charfile);
   printf("File descriptor received is %d\n", filedesc);
   
   char file[10000];

   int numbytes = read(filedesc, file, 10000);
   
   printf("%d bytes read:  %s\n", numbytes, file);
   
   char * charbyte = malloc(10);
   sprintf(charbyte, "%d\n", numbytes);
   
   rio_writen(connfd, charbyte, strlen(charbyte));
   
   sprintf(file, "%s\n", file);
   rio_writen(connfd, file, strlen(file));

}

void work_write(int connfd){

   // recieve and process file descriptor
   char buf[MAXLINE];
   rio_readlineb(&rio, buf, MAXLINE);
   char * charfile = malloc(strlen(buf));
   strcpy(charfile, buf);
   charfile[strlen(buf)-1] = '\0';
   
   sprintf(buf, "PROCEED\n");
	rio_writen(connfd, buf, strlen(buf));
   
   int filedesc = atoi(charfile);
   printf("File descriptor received is %d\n", filedesc);

   //receive text to be written
   char file[10000];
   rio_readlineb(&rio,file, 10000);
   file[strlen(file)-1] = '\0';
   printf("The text you have written is %s\n", file);
   
   sprintf(buf, "PROCEED\n");
	rio_writen(connfd, buf, strlen(buf));

   int success = write(filedesc, file, strlen(file));
   
   printf("Success:  %d\n", success);
   sprintf(buf, "%d/n", success);
   rio_writen(connfd, buf, strlen(buf));

}

void work_close(int connfd){

   char buf[MAXLINE];
   rio_readlineb(&rio, buf, MAXLINE);
   char * charfile = malloc(strlen(buf));
   strcpy(charfile, buf);
   charfile[strlen(buf)-1] = '\0';
   
   sprintf(buf, "PROCEED\n");
	rio_writen(connfd, buf, strlen(buf));
   
   int filedesc = atoi(charfile);
   printf("File descriptor received is %d\n", filedesc);
   
   int success = close(filedesc);
   
   char * charbyte = malloc(10);
   sprintf(charbyte, "%d\n", success);
   
   rio_writen(connfd, charbyte, strlen(charbyte));

}

void * worker(void * vargp){

	pthread_detach(pthread_self());
	size_t n;
	while (1){
		int connfd = sbuf_remove(&sbuf);
		

		printf("Worker thread has accepted client connection...\n");

		char buf[MAXLINE];

		rio_readinitb(&rio, connfd);

		while ((n = rio_readlineb(&rio, buf, MAXLINE)) != 0){
		  		// read command, then call appropriate function
          
			if (!strcmp(buf, "OPEN\n")){
			  sprintf(buf, "PROCEED\n");
			  rio_writen(connfd, buf, strlen(buf));
			  work_open(connfd);
			  break;
			}
			else if (!strcmp(buf, "WRITE\n")){
           sprintf(buf, "PROCEED\n");
			  rio_writen(connfd, buf, strlen(buf));
			  work_write(connfd);
			  break;
			}
			else if (!strcmp(buf, "READ\n")){
           sprintf(buf, "PROCEED\n");
			  rio_writen(connfd, buf, strlen(buf));
			  work_read(connfd);
			  break;
			}
			else if (!strcmp(buf, "CLOSE\n")){
           sprintf(buf, "PROCEED\n");
			  rio_writen(connfd, buf, strlen(buf));
			  work_close(connfd);
			  break;
			}
			else {  // report some sort of error

			}
		}
		printf("Closing\n");
		close(connfd);
	}
}

int main(int argc, char ** argv){

	int i, listenfd, connfd, port;
	socklen_t clientlen = sizeof(struct sockaddr_in);
	struct sockaddr_in clientaddr;
	pthread_t tid;

	if (argc != 2){
		fprintf(stderr, "usage:  %s <port>\n", argv[0]);
		exit(0);
	}

	port = atoi(argv[1]);
	sbuf_init(&sbuf, SBUFSIZE);
	listenfd = open_listenfd(port);

	for (i = 0; i < NTHREADS; i++){					// create the worker threads
		pthread_create(&tid, NULL, worker, NULL);
	}

	char hostname[1024];
	gethostname(hostname, 1024);

	printf("Server %s ready on port %d\n", hostname, port);

	while (1){
		connfd = accept(listenfd, (SA*)&clientaddr, &clientlen);
		sbuf_insert(&sbuf, connfd);
	}

	printf("\nServer terminating...\n");
	return 0;
}

