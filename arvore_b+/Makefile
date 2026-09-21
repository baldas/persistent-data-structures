LDFLAGS = -lpmemobj

LINKER=$(CC)

CFLAGS=-std=gnu99

.PHONY:	all clean


all: persistent

arvore_persistente: arvore_persistente.c clean
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

arvore_volatil: arvore_volatil.c clean
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

volatile: arvore_volatil
	./arvore_volatil

persistent: arvore_persistente
	./arvore_persistente

test: arvore_persistente
	./arvore_persistente 10

massive_test: arvore_persistente.c veryclean
	$(CC) $< -o $(BINS)  $(LDFLAGS) -DMASSIVE_TEST
	$(CC) multi_test.c -o multi_test
	./multi_test

clean:
	rm -f arvore_persistente arvore_volatil multi_test

veryclean: clean
	rm -f *~ arvore_pool*.obj
