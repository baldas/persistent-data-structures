
#
#

LDFLAGS = -lpmemobj

BINS = hash_persistente

LINKER=$(CC)

CFLAGS=-std=gnu99

.PHONY:	all clean


all:	$(BINS)

$(BINS): hash_persistente.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

execute: clean $(BINS)
	./$(BINS)

test: clean $(BINS)
	./$(BINS) 10

massive_test: hash_persistente.c clean
	$(CC) $< -o $(BINS)  $(LDFLAGS) -DMASSIVE_TEST
	$(CC) multi_test.c -o multi_test
	./multi_test

clean:
	rm -f $(BINS) make_test hash_pool*.obj
