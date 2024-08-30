EXE=fetchmail

$(EXE): main.c
	gcc -Wall -g main.c -lm -o $(EXE)

format:
	clang-format -style=file -i *.c

clean: 
	rm -f fetchmail
