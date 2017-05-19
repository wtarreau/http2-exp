CFLAGS = -O2
OBJS = mini-enc gen-rht

all: $(OBJS)

%: %.c

clean:
	-rm -vf $(OBJS)
