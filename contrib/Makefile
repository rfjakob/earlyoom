zombie: *.c
	gcc -Wall -Wextra -o zombie zombie.c

.PHONY: format
format:
	autopep8 -i *.py
	clang-format --style=file -i *.c
