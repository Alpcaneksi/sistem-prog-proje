all: tarsau

tarsau: tarsau.c
	gcc -Wall -Wextra -g -o tarsau tarsau.c

clean:
	rm -f tarsau