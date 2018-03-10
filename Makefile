CC=gcc
OSS_OBJECTS=oss.o IPCUtilities.o ErrorLogging.o 
USER_OBJECTS=user.o 
LINKEDLBS=-lrt
CFLAGS=-w

default: oss 

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

oss: $(OSS_OBJECTS)
	$(CC) $(CFLAGS) $(LINKEDLBS) $(OSS_OBJECTS) -o oss

user: $(USER_OBJECTS)
	$(CC) $(CFLAGS) $(LINKEDLBS) $(USER_OBJECTS) -o user

clean:
	rm -f oss
	rm -f user
	rm -f *.o
	rm -f *.log
