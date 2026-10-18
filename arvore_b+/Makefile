LDFLAGS = -lpmemobj

LINKER=$(CC)

CFLAGS=-std=gnu99

.PHONY:	all clean


all: persistent

arvore_persistente: arvore_persistente.c clean
	$(CC) $(CFLAGS) arvore_persistente.c -o $@ $(LDFLAGS)

arvore_volatil: arvore_volatil.c clean
	$(CC) $(CFLAGS) arvore_volatil.c -o $@ $(LDFLAGS)

volatile: arvore_volatil
	./arvore_volatil

persistent: arvore_persistente
	./arvore_persistente

massive_test: arvore_persistente.c arvore_volatil.c veryclean
	mkdir -p Pools
	mkdir -p Resultados
	mkdir -p Testes
	$(CC) $(CFLAGS) -DMASSIVE_TEST arvore_persistente.c  -o arvore_persistente $(LDFLAGS)
	$(CC) $(CFLAGS) -DMASSIVE_TEST arvore_volatil.c -o arvore_volatil $(LDFLAGS)
	$(CC) $(CFLAGS) multi_test.c -o multi_test
	./multi_test

clean:
	rm -f arvore_persistente arvore_volatil multi_test

veryclean: clean
	rm -f *~
	rm -rf Pools
