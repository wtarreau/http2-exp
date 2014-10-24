CFLAGS = -O2
OBJS = mini-enc

all: $(OBJS)

%: %.c

clean:
	-rm -vf $(OBJS)
