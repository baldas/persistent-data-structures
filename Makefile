# Caminho para as subpastas das estruturas
DIR_HASH   = ./hash
DIR_ARVORE = ./arvore_b+

# Alvo padrão: apenas avisa os comandos disponíveis
.PHONY: all massive_test clean

all: massive_test

massive_test:
	$(MAKE) -C $(DIR_HASH) massive_test
	$(MAKE) -C $(DIR_ARVORE) massive_test

clean:
	$(MAKE) -C $(DIR_HASH) clean
	$(MAKE) -C $(DIR_ARVORE) clean
	
veryclean:
	$(MAKE) -C $(DIR_HASH) veryclean
	$(MAKE) -C $(DIR_ARVORE) veryclean
