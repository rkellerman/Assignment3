## Makefile
CC = gcc
CFLAGS = -Wall -g
OBJS = server.o client.o
all: server client



server: $(OBJS)
	$(CC) -g -pthread -o server server.o 

client: $(OBJS)
	$(CC) -g -o client client.o

client.o: client.c header.h
	$(CC) -c -g client.c 

server.o: server.c header.h
	$(CC) -c -pthread -g server.c