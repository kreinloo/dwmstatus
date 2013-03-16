CC=gcc
CFLAGS=-std=c99 -pedantic -Wall
LDFLAGS=-lX11 -lasound
BIN=dwmstatus

$(BIN): dwmstatus.c config.h
	$(CC) dwmstatus.c $(CFLAGS) $(LDFLAGS) -o $(BIN)

clean:
	rm $(BIN)
