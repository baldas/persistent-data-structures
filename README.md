# Estruturas de Dados Persistentes com PMDK

Este repositório reúne implementações de estruturas de dados persistentes em C com base no conjunto de bibliotecas do PMDK (`Persistent Memory Development Kit`), especialmente `libpmemobj`.

A ideia principal é demonstrar como armazenar e manipular dados em memória persistente, preservando o estado entre execuções e permitindo que estruturas como hash e árvores sobrevivam depois do encerramento do processo.

## Objetivo

- Explorar conceitos de memória persistente em sistemas em C.
- Implementar estruturas de dados executadas em memória não volátil.
- Usar `libpmemobj` como camada de abstração para gerenciar objetos persistentes.
- Implementar estruturas de dados que persistam em memória persistente (PMDK).
- Comparar implementações persistentes com versões voláteis em memória RAM.

## Estrutura do repositório

```text
estruturas-persistentes/
├── README.md
├── hash/
│   ├── hash_persistente.c
│   ├── hash_volatil.c
│   ├── multi_test.c
│   ├── Makefile
│   ├── test.txt
│   └── README.md
├── arvore_b+/
│   └── ...
└── ...
```

### Diretórios

- `hash/` — implementação de tabela hash persistente e versão volátil de referência.
- `arvore_b+/` — espaço para implementação de árvore B+ persistente (em desenvolvimento ou extensão do repositório).

## Tecnologias usadas

- C (linguagem principal)
- PMDK (`libpmemobj`)
- Sistema de arquivos e pools persistentes
- Estruturas de dados voláteis para comparação

## Estrutura de dados incluída

### Hash persistente

A pasta `hash` contém uma implementação de tabela hash com:

- inserção de valores
- busca por valor
- remoção por valor ou posição
- restauração de itens removidos logicamente
- expansão e redução automática da capacidade
- persistência em pool PMEM
- testes em lote e execução em paralelo

## Dependências

Para compilar os exemplos, é necessário ter instalado o PMDK e os headers da biblioteca `libpmemobj`.

Em distribuições Linux, normalmente isso requer a instalação dos pacotes do PMDK ou a compilação do projeto a partir do código-fonte do PMDK.

Exemplo:
```bash
sudo apt-get update
sudo apt-get install libpmemobj-dev
```

## Como compilar

Acesse a pasta da estrutura desejada e rode:

```bash
make
```

Exemplo:

```bash
cd hash
make
```

## Como executar

### Hash persistente

```bash
cd hash
./hash_persistente
```

Também é possível passar um argumento para controlar a vida útil do programa:

```bash
./hash_persistente 10
```

### Testes em lote

```bash
make massive_test
```

ou utilizar o utilitário de execução paralela:

```bash
./multi_test
```

## Observações importantes

- O PMDK exige atenção ao uso de transações e persistência de objetos em pool.
- Estruturas persistentes são sensíveis a integridade de dados e a operações de liberação/reestruturação.
- O repositório foi organizado para servir de base didática e experimental para estudos de memória persistente.

## Casos de uso

- Prototipação de estruturas de dados em memória não volátil.
- Educação em persistência de dados em C.
- Exploração de PMDK como ferramenta para sistemas de alto desempenho.
- Testes de comportamento de estruturas sob expansão e redução.

## Contribuição

Este repositório pode ser expandido com novas estruturas persistentes, como:

- árvores B*
- árvores AVL
- filas e pilhas

## Licença

Este projeto não possui uma licença explicitamente definida no repositório. Consulte o autor ou o ambiente de uso antes de redistribuir o código em outros contextos.
