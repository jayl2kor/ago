CC      = cc
CFLAGS  = -std=c11 -Wall -Wextra -Werror -pedantic
CFLAGS_DEBUG = $(CFLAGS) -g3 -fsanitize=address,undefined -DAGO_DEBUG
DEPFLAGS = -MMD -MP

LIB_SRC = $(filter-out src/main.c, $(wildcard src/*.c))
OBJ     = $(LIB_SRC:.c=.o)
DEPS    = $(OBJ:.o=.d) src/main.d

all: ago

ago: $(OBJ) src/main.o
	$(CC) $(CFLAGS_DEBUG) -o $@ $^

src/%.o: src/%.c
	$(CC) $(CFLAGS_DEBUG) $(DEPFLAGS) -c -o $@ $<

tests/test_%: tests/test_%.c $(OBJ)
	$(CC) $(CFLAGS_DEBUG) -Isrc -o $@ $^

test:
	@echo "No tests yet"

clean:
	rm -f src/*.o src/*.d ago tests/test_lexer tests/test_parser tests/test_typechecker tests/test_interpreter tests/test_integration

-include $(DEPS)
.PHONY: all test clean
