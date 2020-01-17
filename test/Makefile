CFLAGS = -O3 -c -g
LIB = -pthread

SRCS = sir_char_reader.c
OBJS = $(patsubst %.c, %.o, $(SRCS))

sir_char_reader : $(OBJS)
	$(CC) -o sir_char_reader $(OBJS) $(LIB)

%.o: %.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f *.o sir_char_reader

.PHONY: clean