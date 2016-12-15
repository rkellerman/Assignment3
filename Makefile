
all: server client.o test

server: server.c
	gcc -Wall -pthread -g -o server server.c

client.o: client.c client.h
	gcc -Wall -g -c client.c

test: client.o client.h test.c
	gcc -Wall -g -o test test.c client.o

clean:
	rm *.o server test
