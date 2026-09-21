LDFLAGS = -lpmemobj

LINKER=$(CC)

CFLAGS=-std=gnu99

.PHONY:	all clean


all: persistent

hash_persistente: hash_persistente.c clean
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

hash_volatil: hash_volatil.c clean
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

volatile: hash_volatil
	./hash_volatil

persistent: hash_persistente
	./hash_persistente

test: hash_persistente
	./hash_persistente 10

massive_test: hash_persistente.c veryclean
	$(CC) $< -o $(BINS)  $(LDFLAGS) -DMASSIVE_TEST
	$(CC) multi_test.c -o multi_test
	./multi_test

clean:
	rm -f hash_persistente hash_volatil multi_test

veryclean: clean
	rm -f *~ hash_pool*.obj
