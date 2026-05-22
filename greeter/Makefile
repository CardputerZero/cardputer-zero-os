CC ?= cc
CFLAGS ?= -Wall -Wextra -O2
LDFLAGS ?=
LDLIBS ?= -lpam

all: zero-greeter

zero-greeter: zero-greeter.c pam_auth.c pam_auth.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ zero-greeter.c pam_auth.c $(LDLIBS)

clean:
	rm -f zero-greeter
