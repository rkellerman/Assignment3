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

#define UNRESTRICTED 0
#define EXCLUSIVE    1
#define TRANSACTION  2

#define NTHREADS 4
#define SBUFSIZE 16
#define LISTENQ  1024
#define MAXLINE     8192
#define RIO_BUFSIZE 8192

#define PORT 10062



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
sem_t lock;
sem_t wlock;
sem_t lock2;


typedef struct {
	int rio_fd;                /* Descriptor for this internal buf */
	int rio_cnt;               /* Unread bytes in internal buf */
	char *rio_bufptr;          /* Next unread byte in internal buf */
	char rio_buf[RIO_BUFSIZE]; /* Internal buffer */
} rio_t;

typedef struct fileNode{
   char * filename;
   int flag;
   struct fileNode * next;
} fileNode;

typedef struct connectionNode{
   int connfd;
   char * filename;
   int fileflag;
   struct connectionNode * next;
} connectionNode;

typedef struct {
   int size;
   struct connectionNode * front;
} queue;

typedef struct fdNode{
   int fd;
   char * filename;
   struct fdNode * next;

} fdNode;

queue * q;
fileNode * front;
fdNode * fdfront;

int insert(char * filename, int fd){
   fdNode * ptr = (fdNode*)malloc(sizeof(fdNode));
   ptr->filename = filename;
   ptr->fd = fd;
   ptr->next = fdfront;
   fdfront = ptr;
   
   return 0;
}

int delete(int fd){
   fdNode * ptr = fdfront;
   fdNode * prev = NULL;
   if (ptr == NULL){
      return -1;
   }
   if (fdfront->fd == fd){
      fdfront = fdfront->next;
      return 0;
   }
   while (ptr != NULL){
      if (fd == ptr->fd){
         prev->next = ptr->next;
         free(ptr);
         return 0;
      }
      prev = ptr;
      ptr = ptr->next;
   }
   
   return -1;
}

void find(char * filename, int fd){
   fdNode * ptr = fdfront;
   while (ptr != NULL){
      if (fd == ptr->fd){
         memcpy(filename, ptr->filename, strlen(ptr->filename));
         return;
      }
      ptr = ptr->next;
   }
   return;
}

int enqueue(int connfd, int fileflag, char * filename){ 
      
   connectionNode * ptr = q->front;
   if (ptr == NULL){    // no items exist, insert at the front
      ptr = (connectionNode*)malloc(sizeof(connectionNode));
      ptr->connfd = connfd;
      ptr->fileflag = fileflag;
      ptr->filename = filename;
      ptr->next = NULL;
      q->front = ptr;
      q->size++;
      return 0;           
   }
   else {
      while (ptr->next != NULL){
         ptr = ptr->next;
      }
      connectionNode * temp = (connectionNode*)malloc(sizeof(connectionNode));
      temp->connfd = connfd;
      temp->fileflag = fileflag;
      temp->filename = filename;
      temp->next = NULL;
      ptr->next = temp;
      q->size++;
      return 0;
   }

}

connectionNode* dequeue(){

   connectionNode * ptr = q->front;
   if (ptr == NULL){
      return NULL;   
   }
   else {
      q->front = ptr->next;
      q->size--;
      return ptr;
   }

}

// returns -1 if file already exists and blocks current operation, 0 if not blocked or inserted successfully
int getorset(char * filename){  // check list for current file and return value, insert if not there --- ONLY FOR OPEN FUNCTION
   fileNode * ptr = front;
   fileNode * prev = NULL;
   while (ptr != NULL){ 
      
      if (!strcmp(filename, ptr->filename)){    // file is currently in the list
         int flag = ptr->flag;
         if (flag == 0){
            ptr->flag = 1;
            return 0;
         }
         else if (flag == 1){  // block the current operation, send a -1
            return -1;
         }
      }
      else {
         prev = ptr;
         ptr = ptr->next;
      }
   }
   // if we've made it here, it doesn't exist in the list
   if (front == NULL){
   	ptr = (fileNode*)malloc(sizeof(fileNode));
      ptr->filename = filename;
      ptr->flag = 1;
      ptr->next = NULL;
      front = ptr;
      return 0;
   }
   else {
   	ptr = (fileNode*)malloc(sizeof(fileNode));
      ptr->filename = filename;
      ptr->flag = 1;
      ptr->next = NULL;
      prev->next = ptr;
      return 0;
   }
}

int set(int fd){     // uses file descriptor to get filename, then use filename to find entry in list, and set flag to 0, for use on CLOSE

    char filename[0xFFF];
    find(filename, fd);
    
    fileNode * ptr = front;
    while (ptr != NULL){
    	
    	 
       if (!strcmp(filename, ptr->filename)){
          ptr->flag = 0;
          return 0;     // success
       }
       ptr = ptr->next;
    }
    return -1;          // error
}

typedef struct sockaddr SA;

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
sbuf_t sbuf_read, sbuf_write;
sbuf_t sbuf_segment, sbuf_wsegment;
rio_t rio;
int init = 0;
int fileMode = -1; 
int iterations;           
int port;
char * bigfile;

int work_open(int connfd){ 			// be sure to use semaphores
	
	
	char buf[MAXLINE];
	
	rio_readlineb(&rio, buf, MAXLINE);
   char * pathname = malloc(strlen(buf));
   strcpy(pathname, buf);
   pathname[strlen(buf)-1] = '\0';
   
	
	sprintf(buf, "PROCEED\n");
   rio_writen(connfd, buf, strlen(buf));
   
   char * flags = malloc(2);
   rio_readlineb(&rio, flags, MAXLINE);
   flags[1] = '\0';

   
   sprintf(buf, "PROCEED\n");
   rio_writen(connfd, buf, strlen(buf));
   
   printf("Pathname is \"%s\"\n", pathname);
   
   int flag = atoi(flags);
   
   // perform the check here
   if (fileMode == UNRESTRICTED){
      // no need to do any checking
   }
   else if (fileMode == EXCLUSIVE){
      if (flag == O_RDONLY){
         // no need to do any checking
      } 
      else if (flag == O_WRONLY || flag == O_RDWR){
         int response = getorset(pathname);
         if (response == 0){
            // all is well, we can proceed
         }
         else if (response == -1){
            // need to send this request to the queue for later
            enqueue(connfd, flag, pathname);
            printf("File already open in write mode, please wait...\n");
            sprintf(buf, "WAIT\n");
			   rio_writen(connfd, buf, strlen(buf));
            return -1;
         }
      }
   }
   else if (fileMode == TRANSACTION){
      if (flag == O_RDONLY || flag == O_WRONLY || flag == O_RDWR){
         int response = getorset(pathname);
         printf("RESPONSE FROM GET OR SET IS %d\n", response);
         if (response == 0){
            // all is well, we can proceed
         }
         else if (response == -1){
            // need to send this request to the queue for later
            enqueue(connfd, flag, pathname);
            sprintf(buf, "WAIT\n");
			   rio_writen(connfd, buf, strlen(buf));
            printf("File already open in write mode, please wait...\n");
            return -1;
         }
      }
   }
   
   int filedesc = open(pathname, flag);
   
   int success = insert(pathname, filedesc);

   
   // printf("Flag is %d, length of pathname is %d\n", flag, strlen(pathname));
   printf("File descriptor created: %d\n", filedesc);
   
   char * charfile = malloc(3);
   sprintf(charfile, "%d\n", filedesc);
   rio_writen(connfd, charfile, strlen(charfile));
   
   //free(pathname);
   free(flags);
   free(charfile);
  

	return 0;
}

int bigread(void * vargp){
   
   pthread_detach(pthread_self());
   
    while (1){
    	
        int connfd = sbuf_remove(&sbuf_read);
        int segment = sbuf_remove(&sbuf_segment);
        
        
        printf("Connection accepted:  %d, Servicing segment:  %d\n", connfd, segment);
        
        char bigfilesegment[(int)pow(2, 11) + 10]; 					// create an array capable of holding the maximum amount of text

	     int startIndex = (segment)*(int)pow(2, 11);

	     printf("\nI started at %d\n", startIndex);

	     if (segment + 1 < iterations){
		     memcpy(bigfilesegment, &bigfile[startIndex], (int)pow(2,11));
		     bigfilesegment[(int)pow(2, 11)] = '\0';
		     sprintf(bigfilesegment, "%s\n", bigfilesegment);
		     printf("%s", bigfilesegment);
	     }
	     else {
	 	     int len = strlen(bigfile) - (int)pow(2, 11)*(iterations - 1);
		     printf("len = %d\n", len);
		     memcpy(bigfilesegment, &bigfile[startIndex], len);
		     bigfilesegment[len] = '\0';
		     sprintf(bigfilesegment, "%s\n", bigfilesegment);
		     printf("%s", bigfilesegment);
	     }
	     
	     sem_wait(&lock);
	     
	     rio_writen(connfd, bigfilesegment, strlen(bigfilesegment));
	     char * charfile = malloc(3);
        sprintf(charfile, "%d\n", segment);
        rio_writen(connfd, charfile, strlen(charfile));
        
        close(connfd);
        free(charfile);
        
        sem_post(&lock);
	     
        
    
    
    }
}

void work_read(int connfd){

   char buf[MAXLINE];
   rio_readlineb(&rio, buf, MAXLINE);
   char * charfile = malloc(strlen(buf));
   strcpy(charfile, buf);
   charfile[strlen(buf)-1] = '\0';
   
   
   
   int filedesc = atoi(charfile);
   printf("File descriptor received is %d\n", filedesc);
   
   char file[100000];

   int numbytes = read(filedesc, file, 100000);
   
   
   
   if (numbytes < (int)pow(2, 11)){
      sprintf(buf, "PROCEED\n");
	   rio_writen(connfd, buf, strlen(buf));
	}
	else {
	   
	   // do stuff for sending back several buffers of read stuff
	   
	   socklen_t clientlen = sizeof(struct sockaddr_in);
	   struct sockaddr_in clientaddr;
	   
	   bigfile = (char*)malloc(numbytes);
	   memcpy(bigfile, file, numbytes);
	   
	   iterations = (int)(numbytes / pow(2, 11)) + 1;
	   
	   sprintf(buf, "%d\n", iterations);
	   rio_writen(connfd, buf, strlen(buf));
	   
	   close(connfd);
	   
	   int i;
	   pthread_t tids[iterations];
	   int * listenfds = (int*)malloc(iterations*sizeof(int));
	   
	   sem_init(&lock, 0, 1);	
	   sbuf_init(&sbuf_read, 10);
	   sbuf_init(&sbuf_segment, 10);
	   
	   for(i = 0; i < iterations; i++){
	       listenfds[i] = open_listenfd(port + i + 47);
	       printf("PORT IS %d\n", port + i + 47);
	   }
	   
	   for (i = 0; i < iterations; i++){
	      connfd = accept(listenfds[i], (SA*)&clientaddr, &clientlen);
	      sbuf_insert(&sbuf_read, connfd);
	      sbuf_insert(&sbuf_segment, i);
	      close(listenfds[i]);
	   }
	   
	   
	   for (i = 0; i < 10; i++){
	       
	       pthread_create(&tids[i], NULL, bigread, NULL);
	   }
	   
	   free(listenfds);
	   //free(bigfile);
	   free(charfile);
	   
	   
	   
	   return;
	}
   
   printf("%d bytes read:  %s\n", numbytes, file);
   
   char * charbyte = malloc(10);
   sprintf(charbyte, "%d\n", numbytes);
   
   rio_writen(connfd, charbyte, strlen(charbyte));
   
   sprintf(file, "%s\n", file);
   rio_writen(connfd, file, strlen(file));
   
   free(charbyte);
   
   
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
   char file[100000];
   rio_readlineb(&rio,file, 100000);
   file[strlen(file)-1] = '\0';
   printf("The text you have written is %s\n", file);
   
   sprintf(buf, "PROCEED\n");
	rio_writen(connfd, buf, strlen(buf));

   int success = write(filedesc, file, strlen(file));
   
   printf("Success:  %d\n", success);
   sprintf(buf, "%d\n", success);
   rio_writen(connfd, buf, strlen(buf));
   
   sleep(1);
   
   free(charfile);
   
   

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
   
   set(filedesc);
   int result = delete(filedesc);
   
   
   
   char * charbyte = malloc(10);
   sprintf(charbyte, "%d\n", success);
   
   rio_writen(connfd, charbyte, strlen(charbyte));
   
   free(charfile);
   free(charbyte);
   

}

char * writefile;
int num = 0;

void * bigwrite(int portport){

    pthread_detach(pthread_self());
    char bigfilesegment[(int)pow(2, 11) + 2]; 
   
    while (1){
    	
    	
        int connfd = sbuf_remove(&sbuf_write);
        int segment = sbuf_remove(&sbuf_wsegment);
        
        sem_wait(&wlock);
        
        rio_readinitb(&rio, connfd);
        
        printf("Connection accepted:  %d\n", connfd);
        
        rio_readlineb(&rio, bigfilesegment, (int)pow(2, 11) + 2);
        bigfilesegment[strlen(bigfilesegment)-1] = '\0';
        
        
        //printf("Segment %d received:\n%s\n", segment, bigfilesegment);
        //printf("Written %d bytes\n\n", (int)strlen(bigfilesegment));
        
        sem_post(&wlock);
        
        while (segment != num){
            
        }
        
        writefile = strcat(writefile, bigfilesegment);
        
        sem_wait(&wlock);
        num = num + 1;
        sem_post(&wlock);
        
     }




}

int work_longwrite(int connfd){

    char buf[MAXLINE];
    rio_readlineb(&rio, buf, MAXLINE);
    
    char * charfile = malloc(strlen(buf));
    strcpy(charfile, buf);
    charfile[strlen(buf)-1] = '\0';
    
    int iterations = atoi(buf);
    printf("The server recieved:  %d iterations\n", iterations);
    
    sprintf(buf, "PROCEED\n");
	 rio_writen(connfd, buf, strlen(buf));
	 
	 rio_readlineb(&rio, buf, MAXLINE);
	 strcpy(charfile, buf);
    charfile[strlen(buf)-1] = '\0';
    
    int filedesc = atoi(charfile);
    
    sprintf(buf, "PROCEED\n");
	 rio_writen(connfd, buf, strlen(buf));
	 
	 socklen_t clientlen = sizeof(struct sockaddr_in);
	 struct sockaddr_in clientaddr;
	   
	 int i;
	 pthread_t tids[iterations];
	 int * listenfds = (int*)malloc(iterations*sizeof(int));
	   
	 sem_init(&wlock, 0, 1);	
	 sbuf_init(&sbuf_write, 10);
	 sbuf_init(&sbuf_wsegment, 10);
	   
	 for(i = 0; i < iterations; i++){
	     listenfds[i] = open_listenfd(port + i + 1001);
	 }
	   
	 for (i = 0; i < iterations; i++){
	    connfd = accept(listenfds[i], (SA*)&clientaddr, &clientlen);
	    sbuf_insert(&sbuf_write, connfd);
	    sbuf_insert(&sbuf_wsegment, i);
	    close(listenfds[i]);
    }
    
    writefile = (char*)malloc(iterations*(int)pow(2, 11));
    sprintf(writefile, "");
    
    num = 0;
    
    for (i = 0; i < 10; i++){
	       
	     pthread_create(&tids[i], NULL, bigwrite, port + i + 1);
	 }
	 
	 while (num != iterations){
	 }
	 
	 printf("%s\n", writefile);
	 
	 int success = write(filedesc, writefile, strlen(writefile));
	 
	 
    int listenfd = open_listenfd(port + 2000);
    connfd = accept(listenfd, (SA*)&clientaddr, &clientlen);
    printf("Connection %d\n", connfd);
    close(listenfd);
    printf("WROTE %d BYTES\n", success);
    sprintf(buf, "%d\n", success);
    rio_writen(connfd, buf, strlen(buf));
    
    
    close(connfd);
    free(charfile);
    free(listenfds);
    //free(writefile);
    
    
    return 0;
	   

}

int initialize(int connfd){
	
	errno = 0;

   char buf[MAXLINE];

   if (init == 1){
      sprintf(buf, "\n");
	   rio_writen(connfd, buf, strlen(buf));
	   return 0;
   }
   sprintf(buf, "PROCEED\n");
	rio_writen(connfd, buf, strlen(buf));
   
   
   
   rio_readlineb(&rio, buf, MAXLINE);
   char * charfile = malloc(strlen(buf));
   strcpy(charfile, buf);
   charfile[strlen(buf)-1] = '\0';
	
	fileMode = atoi(charfile);
	if (fileMode < 3 && fileMode > -1){
	  init = 1;
	  printf("Server running in filemode %d\n", fileMode);
	}
	else {
	   sprintf(buf, "\n");
	   rio_writen(connfd, buf, strlen(buf));
	   fileMode = -1;
	   return -1;
	}
	
	sprintf(buf, "PROCEED\n");
	rio_writen(connfd, buf, strlen(buf));
	
	free(charfile);
	
	
	

   return 0;
}

void * worker(void * vargp){

	pthread_detach(pthread_self());
	size_t n;
	while (1){
		int connfd = sbuf_remove(&sbuf);
		sem_wait(&lock2);

		printf("Worker thread has accepted client connection...\n");

		char buf[MAXLINE];

		rio_readinitb(&rio, connfd);

		while ((n = rio_readlineb(&rio, buf, MAXLINE)) != 0){
		  		// read command, then call appropriate function
          
			if (!strcmp(buf, "OPEN\n")){
			  sprintf(buf, "PROCEED\n");
			  rio_writen(connfd, buf, strlen(buf));
			  int result = work_open(connfd);
			  if (result == -1){
			  
			  }
			  else {
			     close(connfd);
			  }
			  break;
			}
			else if (!strcmp(buf, "WRITE\n")){
           sprintf(buf, "PROCEED\n");
			  rio_writen(connfd, buf, strlen(buf));
			  work_write(connfd);
			  close(connfd);
			  break;
			}
			else if (!strcmp(buf, "READ\n")){
           sprintf(buf, "PROCEED\n");
			  rio_writen(connfd, buf, strlen(buf));
			  work_read(connfd);
			  close(connfd);
			  break;
			}
			else if (!strcmp(buf, "CLOSE\n")){
           sprintf(buf, "PROCEED\n");
			  rio_writen(connfd, buf, strlen(buf));
			  work_close(connfd);
			  close(connfd);
			  break;
			}
			else if (!strcmp(buf, "LONG WRITE\n")){
			  sprintf(buf, "PROCEED\n");
			  rio_writen(connfd, buf, strlen(buf));
			  work_longwrite(connfd);
			
			}
			else if (!strcmp(buf, "INIT\n")){
			  
			  int success = initialize(connfd);
			  close(connfd);
			  break;
			}
			else {  // report some sort of error

			}
		}
		printf("Closing\n");
		sem_post(&lock2);
		
	}
}

void * queue_monitor(void * vargp){
	
	char buf[MAXLINE];
	char sub[MAXLINE];
	
	
	while (1){
		
		
		// simply printing the contents of the queue
		sprintf(buf, "");
		
		sleep(10);
		
		sem_wait(&lock2);
		connectionNode * ptr = q->front;
      while (ptr != NULL){
          sprintf(buf, "%s%s | %d | %d --> ", buf, ptr->filename, ptr->fileflag, ptr->connfd);
          ptr = ptr->next;
      }
      sprintf(buf, "%sNULL\n", buf);
      if (!strcmp(buf, "NULL\n")){
         //printf("%s\n", buf);
      }
      else {
        printf("CONTENTS OF THE QUEUE:\n");
        printf("%s\n", buf);
      }
      
      
      // attempt to service item in the queue
      connectionNode * current = dequeue();
      while (current != NULL){
          int response = getorset(current->filename);
          if (response == 0){
             // all is well, we can proceed
             
             int filedesc = open(current->filename, current->fileflag);
             int success = insert(current->filename, filedesc);

   
             // printf("Flag is %d, length of pathname is %d\n", flag, strlen(pathname));
             printf("File descriptor created: %d\n", filedesc);
   
             char * charfile = malloc(3);
             sprintf(charfile, "%d\n", filedesc);
             rio_writen(current->connfd, charfile, strlen(charfile));
             close(current->connfd);
             free(charfile);
             free(current);
             
          }
          else if (response == -1){
             // timeout response is sent, current item is forgotten
             sprintf(sub, "TIMEOUT\n");
			    rio_writen(current->connfd, sub, strlen(sub));
			    close(current->connfd);
			    free(current);
          }
          
          current = dequeue();
       }
       
       
       // printing the contents of the fdlist
       sprintf(buf, "");
       fdNode * ptr2 = fdfront;
       while (ptr2 != NULL){
           sprintf(buf, "%s%s | %d --> ", buf, ptr2->filename, ptr2->fd);
           ptr2 = ptr2->next;
       }
       sprintf(buf, "%sNULL\n", buf);
       if (!strcmp(buf, "NULL\n")){
           // don't print
       }
       else {
       	  printf("CONTENTS OF THE FD - FILENAME LIST:\n");
           printf("%s\n",  buf);
       }
       
       
       sprintf(buf, "");
       fileNode * ptr3 = front;
       while (ptr3 != NULL){
           sprintf(buf, "%s%s | %d --> ", buf, ptr3->filename, ptr3->flag);
           ptr3 = ptr3->next;
       }
       sprintf(buf, "%sNULL\n", buf);
       if (!strcmp(buf, "NULL\n")){
           // don't print
       }
       else {
       	  printf("CONTENTS OF THE OPEN FILENAME LIST:\n");
           printf("%s\n",  buf);
       }
       
       
      sem_post(&lock2);
		
		
	}

   return NULL;
}

int main(int argc, char ** argv){
	
	printf("boop\n");

	int i, listenfd, connfd;
	socklen_t clientlen = sizeof(struct sockaddr_in);
	struct sockaddr_in clientaddr;
	pthread_t tid;

	

	port = PORT;
	sbuf_init(&sbuf, SBUFSIZE);
	sem_init(&lock2, 0, 1);
	listenfd = open_listenfd(port);
	
	// Initialize both the file list and the queue
	
	q = (queue *)malloc(sizeof(queue));
	q->size = 0;
   q->front = NULL;
   
   front = NULL;
   fdfront = NULL;

	for (i = 0; i < NTHREADS; i++){					// create the worker threads
		pthread_create(&tid, NULL, worker, NULL);
	}

	char hostname[1024];
	gethostname(hostname, 1024);

	printf("Server %s ready on port %d\n", hostname, port);
	
	// create thread to monitor the queue and service/execute timeout
	
	pthread_create(&tid, NULL, queue_monitor, NULL);

	while (1){
		connfd = accept(listenfd, (SA*)&clientaddr, &clientlen);
		sbuf_insert(&sbuf, connfd);
	}
	
	

	printf("\nServer terminating...\n");
	return 0;
}






















