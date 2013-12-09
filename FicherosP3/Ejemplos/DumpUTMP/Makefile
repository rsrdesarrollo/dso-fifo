TARGET = dump_utmp

CC = gcc
CPPSYMBOLS=
CFLAGS = -g -Wall $(CPPSYMBOLS)
LDFLAGS = 

OBJS = dump_utmp.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(TARGET)  $(OBJS)

.c.o: 
	$(CC) $(CFLAGS) -I. -c  $<

clean: 
	-rm -f *.o $(TARGET) 
