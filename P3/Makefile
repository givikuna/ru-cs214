CC      = gcc
CFLAGS  = -Wall -Wextra -Werror -std=c99 -g

TARGETS = mysh test_mysh

.PHONY: all test clean tar

all: $(TARGETS)

mysh: mysh.c
	$(CC) $(CFLAGS) -o mysh mysh.c

test_mysh: test_mysh.c
	$(CC) $(CFLAGS) -o test_mysh test_mysh.c

# written like this bc NixOS
# sorry :(
test: all
	./test_mysh
	@if [ -x /bin/echo ]; then \
		./mysh test_inputs.sh; \
	else \
		echo "note: skipping test_inputs.sh (no /bin/echo; So, NixOS!)"; \
	fi

clean:
	rm -f $(TARGETS) submission.tar

tar:
	tar -cvf submission.tar mysh.c test_mysh.c Makefile README test_inputs.sh
