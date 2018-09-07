CC = gcc
CFLAGS = -Wall -g
CFLAGS += -I. -I/usr/local/libuv-1.23.0/include
LDFLAGS += -L. -L/usr/local/libuv-1.23.0/lib -luv
LDFLAGS += -Wl,-rpath=/usr/local/libuv-1.23.0/lib
LDFLAGS += -Wl,-rpath=/usr/local/queue/lib


ALL:
	$(CC) $(CFLAGS) $(LDFLAGS) threadpool.c -o threadpool
clean:
	-rm threadpool
