


#define HEADER_GAURD
#define MAXLINE     8192
#define RIO_BUFSIZE 8192

 #define UNRESTRICTED 0
 #define EXCLUSIVE    1
 #define TRANSACTION  2

 #define PORT 10062

#ifndef BUTTS
#define BUTTS

typedef struct {
	int rio_fd;                /* Descriptor for this internal buf */
	int rio_cnt;               /* Unread bytes in internal buf */
	char *rio_bufptr;          /* Next unread byte in internal buf */
	char rio_buf[RIO_BUFSIZE]; /* Internal buffer */
} rio_t;


#endif


int port, clientfd, filedesc;
int init = 0;   
char * host;
rio_t rio;
int * clientfds;
pthread_mutex_t lock;
int * wclientfds;


void rio_readinitb(rio_t *rp, int fd);
static ssize_t rio_read(rio_t *rp, char *usrbuf, size_t n);
ssize_t rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen);
int open_clientfd(char *hostname, int port);
ssize_t rio_writen(int fd, void *usrbuf, size_t n);
int netopen(char * pathname, int flags);
char * bigread_thread(int clientfd, int i);
int netread(int fildes, char * buf, size_t nbyte);
int findLength(FILE * fp);
int netwrite(int fildes, char * file, size_t size);
int netclose(int fildes);
int netserverinit(char * hostname, int filemode);




