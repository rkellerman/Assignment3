## Makefile
CC = gcc
CFLAGS = -Wall -g
OBJS = server.o client.o
CLIENT_HEADERS = client.h
SERVER_HEADERS = 

all: server client

server: $(OBJS)
	$(CC) -g -pthread -o server server.o 

client: $(OBJS)
	$(CC) -g -o client test.c client.o

client.o: client.c $(CLIENT_HEADERS)
	$(CC) -c -g client.c 

server.o: server.c 
	$(CC) -c -pthread -g server.c

test: client.o
	$(CC) -o test test.c client.o -lpthread

clean:
	rm client server test *.o