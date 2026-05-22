CC = gcc
CFLAGS = -Wall -Wextra -g

all: tarsau

tarsau: tarsau.c
	$(CC) $(CFLAGS) -o tarsau tarsau.c

clean:
	rm -f tarsau