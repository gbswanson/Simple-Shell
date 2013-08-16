all: shell.c
	gcc -g -Wall -o shell shell.c

clean:
	rm shell
