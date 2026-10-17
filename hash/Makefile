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

massive_test: hash_persistente.c veryclean
	mkdir -p Pools
	$(CC) $(CFLAGS) -DMASSIVE_TEST hash_persistente.c -o hash_persistente $(LDFLAGS)
	$(CC) $(CFLAGS) -DMASSIVE_TEST hash_volatil.c -o hash_volatil $(LDFLAGS) 
	$(CC) $(CFLAGS) multi_test.c -o multi_test
	./multi_test

clean:
	rm -f hash_persistente hash_volatil multi_test

veryclean: clean
	rm -f *~ Pools
