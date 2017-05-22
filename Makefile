CFLAGS = -O0 -g
OBJS = mini-enc mini-dec gen-rht

all: $(OBJS)

mini-dec: mini-dec.o hpack-huff.o

%: %.c

clean:
	-rm -vf $(OBJS) *.o *.a *~
