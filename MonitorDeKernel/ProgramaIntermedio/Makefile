CC     = gcc
CFLAGS = -Wall -O2
LIBS   = -lmicrohttpd -lpthread -lpam -lcjson

all: daemon

daemon: daemon.c
	$(CC) $(CFLAGS) -o daemon daemon.c $(LIBS)

clean:
	rm -f daemon