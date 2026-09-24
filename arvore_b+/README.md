# Árvore B+ Persistente

Este diretório contém uma implementação de uma árvore B+ persistente em C
utilizando a biblioteca `libpmemobj` (PMDK). O objetivo é demonstrar uma
árvore com busca binária que persiste seu estado em um pool de memória
persistente, mantendo sempre a ordenação dos elementos pelo uso do insertion
sort.

## Principais arquivos

- `arvore_persistente.c` — implementação da árvore B+ persistente (usa `libpmemobj`):
  operações básicas (inserir, buscar e remover), redimensionamento
  automático e interface interativa por linha de comando.
- `multi_test.c` — utilitários para executar várias instâncias
  do binário em paralelo; úteis para testes massivos de concorrência.
- `arvore_volatil.c` — versão volátil (em memória RAM) da mesma estrutura para
  comparação e testes locais sem dependência de PMDK.
- `test.txt` — arquivo de entrada de exemplo usado pelos testes massivos.
- `Makefile` — regras para compilar `arvore_persistente` e executar testes básicos.

## Dependências

- PMDK (libpmemobj) — necessário para compilar e executar `hash_persistente`.

## Como compilar

1. Certifique-se de ter instalado os headers e bibliotecas do PMDK.
2. No diretório do projeto, execute:

```bash
make
```

## Executando

- Rodar o binário interativo:

```bash
./arvore_persistente
```

- Rodar um teste com tempo de vida (ex.: 10):

```bash
./arvore_persistente 10
```

- Executar o teste em lote (usa `multi_test`):

```bash
make massive_test
```

## Notas

- O repositório contém versões persistente e volátil da árvore e utilitários de
  teste para facilitar experimentos. Comentários no código foram adicionados
  em português para facilitar o entendimento.