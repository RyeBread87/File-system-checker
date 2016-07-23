all:
	gcc -o fscheck fscheck.c -Wall -Werror
clean:
	rm fscheck
