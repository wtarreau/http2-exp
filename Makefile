CFLAGS = -O0 -g
OBJS = mini-enc mini-dec gen-rht

all: $(OBJS)

%: %.c

clean:
	-rm -vf $(OBJS)
