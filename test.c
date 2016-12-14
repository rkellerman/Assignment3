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
#include "client.h"

int main(){

	char * file;
	char buf[MAXLINE];

	char input[MAXLINE];
	char filepath[MAXLINE];

	char * host = malloc(1000);
	sprintf(host, "grep.cs.rutgers.edu");

	while (1){

		printf("Enter:  {OPEN}, {READ}, {WRITE}, {INIT}, or {CLOSE}\n");
		scanf("%s", input);


		if (!strcmp(input, "OPEN")){
			printf("Enter filepath:  ");
			scanf("%s", filepath);
			filedesc = netopen(filepath, 2);
		}
		else if (!strcmp(input, "READ")){
			printf("Enter file descriptor:  ");
			char * charfile = malloc(3);
			scanf("%s", charfile);
			int filedesc = atoi(charfile);
			file = (char*)malloc(10000);
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

			printf("Input from file?  y or n:   ");
			scanf("%s", charfile);

			if (!strcmp(charfile, "y")){

				int i = 0;
				FILE * ptr_file;
				ptr_file = fopen("bigtext.txt", "r");

				if (!ptr_file){ 				// if ptr_file is NULL, there was an error
					printf("ERROR reading file, check file name...\n");
					exit(-1);
				}

				int filelength = findLength(ptr_file) + 10;
				// printf("FILE LENGTH IS %d\n", filelength-10);
				char * filefile = malloc(filelength);

				rewind(ptr_file);

				char ch;
				for (i = 0; (i < filelength - 10) && ((ch = fgetc(ptr_file)) != EOF); i++){
					filefile[i] = ch;
				}
				filefile[i] = '\0';

				rewind(ptr_file);

				fclose(ptr_file);

				int success = netwrite(filedesc, filefile, strlen(filefile));
				printf("Number of bytes written is %d\n", success);


			}
			else {

				printf("Enter text to be written:  ");

				char *text = calloc(1,1);
				int i = 0;
				char * filefile = (char*)malloc(10000);

				while( fgets(filefile, 10000, stdin) ) // break with ^D or ^Z *
				{
					text = realloc( text, strlen(text)+1+strlen(filefile) );
					if( !text ){} // error handling *
					strcat( text, filefile ); // note a '\n' is appended here everytime *
					printf("%s\n", filefile);
					i++;
					if (i == 2){
						break;
					}
				}
				printf("\ntext:\n%s",text);

				memcpy(text, &text[1], strlen(text) - 1);

				int success = netwrite(filedesc, text, strlen(text));
				printf("Number of bytes written is %d\n", success);
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
