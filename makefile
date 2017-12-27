# $@  target
# $<  first dependency 
# $^  all dependencies
# $?  changed dependents
# $(<F) filename of first dependency
# $(<D) directory name of first dependency

CFLAGS := -O2 -Wall -pedantic
INCLUDES := -I.

all:  select

select: select.c
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^

clean: 
	$(RM) select