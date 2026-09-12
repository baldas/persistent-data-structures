# Hash Persistente

Este diretório contém uma implementação de tabela hash persistente em C
utilizando a biblioteca `libpmemobj` (PMDK). O objetivo é demonstrar uma
hash com verifcação linear que persiste seu estado em um pool de memória
persistente.

## Principais arquivos

- `hash_persistente.c` — implementação da hash persistente (usa `libpmemobj`):
  operações básicas (inserir, buscar, remover, restaurar), redimensionamento
  automático e interface interativa por linha de comando.
- `multi_test.c` — utilitários para executar várias instâncias
  do binário em paralelo; úteis para testes massivos de concorrência.
- `hash_volatil.c` — versão volátil (em memória RAM) da mesma estrutura para
  comparação e testes locais sem dependência de PMDK.
- `test.txt` — arquivo de entrada de exemplo usado pelos testes massivos.
- `Makefile` — regras para compilar `hash_persistente` e executar testes básicos.

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
./hash_persistente
```

- Rodar um teste com tempo de vida (ex.: 10):

```bash
./hash_persistente 10
```

- Executar o teste em lote (usa `multi_test`):

```bash
make massive_test
```

## Notas

- O repositório contém versões persistente e volátil da hash e utilitários de
  teste para facilitar experimentos. Comentários no código foram adicionados
  em português para facilitar o entendimento.