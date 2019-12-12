# $@  target
# $<  first dependency
# $^  all dependencies
# $?  changed dependents
# $(<F) filename of first dependency
# $(<D) directory name of first dependency

CFLAGS := -O2 -Wall
INCLUDES := -I.

all:  server sigalrm

server: server.c
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^

sigalrm: sigalrm.c
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^

clean:
	$(RM) server sigalrm

