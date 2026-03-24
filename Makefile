CC      = cc
CFLAGS  = -std=c11 -Wall -Wextra -Werror -pedantic
CFLAGS_DEBUG = $(CFLAGS) -g3 -fsanitize=address,undefined -DAGL_DEBUG
DEPFLAGS = -MMD -MP

# Detect libcurl
CURL_AVAILABLE := $(shell curl-config --libs >/dev/null 2>&1 && echo yes || echo no)
ifeq ($(CURL_AVAILABLE),yes)
  CURL_CFLAGS  = -DAGL_HAS_CURL
  CURL_LDFLAGS = -lcurl
else
  CURL_CFLAGS  =
  CURL_LDFLAGS =
endif

LIB_SRC = $(filter-out src/main.c, $(wildcard src/*.c))
OBJ     = $(LIB_SRC:.c=.o)
DEPS    = $(OBJ:.o=.d) src/main.d

all: agl

agl: $(OBJ) src/main.o
	$(CC) $(CFLAGS_DEBUG) -o $@ $^ $(CURL_LDFLAGS)

src/%.o: src/%.c
	$(CC) $(CFLAGS_DEBUG) $(CURL_CFLAGS) $(DEPFLAGS) -c -o $@ $<

tests/test_%: tests/test_%.c $(OBJ)
	$(CC) $(CFLAGS_DEBUG) $(CURL_CFLAGS) -Isrc -o $@ $^ $(CURL_LDFLAGS)

test: tests/test_lexer tests/test_parser tests/test_sema tests/test_interpreter tests/test_gc tests/test_vm
	@for t in $^; do echo ""; ./$$t || exit 1; done

clean:
	rm -f src/*.o src/*.d agl tests/test_lexer tests/test_parser tests/test_sema tests/test_typechecker tests/test_interpreter tests/test_gc tests/test_integration

-include $(DEPS)
.PHONY: all test clean
