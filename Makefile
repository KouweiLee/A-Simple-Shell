
.PHONY: clean

all: myshell.c
	gcc myshell.c -o myshell

clean:
	rm -rf *.o
