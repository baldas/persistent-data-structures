// Hash persistente em C usando libpmemobj (Kit de Desenvolvimento de Memória Persistente - PMDK)
// Este arquivo implementa uma tabela hash aberta com verificação linear
// e armazenamento persistente via libpmemobj. Comentários adicionados
// para explicar as principais seções e funções do código.

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <ctype.h>

// Inclusão da biblioteca para programação em memória persistente (PMDK)
#include <libpmemobj.h>

#define EMPTY 0.0
#define FULL 1.0
#define false 0
#define true 1
#define DEFAULT INT_MIN

// Configurações de buffers e da hash
#define BUFFER_SIZE 64
#define INITIAL_SIZE 8
#define SIZE_RATE 2
#define EXPAND_RATE 0.75
#define REDUCTION_RATE (EXPAND_RATE / SIZE_RATE)

// Configurações do pool persistente
#define LAYOUT_NAME "HASH"
#define KB 1024ULL
#define MB (1024ULL * KB)
#define GB (1024ULL * MB)
#define POOL_SIZE PMEMOBJ_MIN_POOL
#define POOL_NAME "hash_pool"

// Inicialização de variáveis e do pool
int lifetime = DEFAULT;
char pool_name[BUFFER_SIZE] = "default";

POBJ_LAYOUT_BEGIN(HASH);
  POBJ_LAYOUT_ROOT(HASH, struct my_root);
  POBJ_LAYOUT_TOID(HASH, int);
  POBJ_LAYOUT_TOID(HASH, char);
  POBJ_LAYOUT_TOID(HASH, struct hash);
POBJ_LAYOUT_END(HASH);

// Definição das estruturas de dados persistentes.
// `struct hash` mantém o estado da tabela hash (persistido no pool).
// - size: número de elementos atualmente marcados como ocupados
// - max_size: capacidade atual (número de slots)
// - data: array persistente de inteiros armazenados
// - occupied: array persistente de flags (char) indicando ocupação
struct hash {
  int size;
  int max_size;
  TOID(int) data;
  TOID(char) occupied;
};

// `my_root` é o root object do pool PMEM e aponta para a tabela hash
struct my_root {
  TOID(struct hash) p_hash;
};

// Imprime a tabela hash em linhas de `INITIAL_SIZE` colunas.
// Marcação usada na saída:
//  - [n]   : valor n presente e marcado como ocupado
//  - [!n]  : valor n presente, mas marcado como removido (flag occupied == false)
//  - [*]   : slot vazio (DEFAULT)
void display(TOID(struct hash) p_aux, FILE* output_file) {

  // Exibe a tabela em blocos de INITIAL_SIZE para melhor legibilidade
  for (int i = 0; i < (D_RO(p_aux)->max_size / INITIAL_SIZE); i++) {

    // Cabeçalho da linha
    if (i != 0)
      fprintf(output_file, "       ");
    else
      fprintf(output_file, "\n HASH =");

    // Percorre cada slot da linha
    for (int j = 0; j < INITIAL_SIZE; j++) {

      // Se o slot não contiver o valor DEFAULT, há algo para mostrar
      if (D_RO(D_RO(p_aux)->data)[i*INITIAL_SIZE+j] != DEFAULT) {

        // Se `occupied` for true, o item está ativo; senão, é um item removido
        if (D_RO(D_RO(p_aux)->occupied)[i*INITIAL_SIZE+j])
            fprintf(output_file, " [%d] ", D_RO(D_RO(p_aux)->data)[i*INITIAL_SIZE+j]);
        else
            fprintf(output_file, " [!%d] ", D_RO(D_RO(p_aux)->data)[i*INITIAL_SIZE+j]);

      } else
        fprintf(output_file, " [*] ");
    }

    fprintf(output_file, " (%d)\n", (i+1)*INITIAL_SIZE);
  }
}

// Função de espalhamento (hash). Nesse projeto usa-se uma função simples
// que calcula (dado * dado) % max_size. Pode ser substituída por outra.
int hash_function(int dado, int max_size) {
  int res = (int) (((long long) dado * dado) % max_size);
  return res < 0 ? res + max_size : res; // Garantia contra resultado negativo
}

// Retorna a taxa de ocupação (load factor) da tabela hash
double hash_rate(TOID(struct hash) p_aux) {
  return (((double) D_RO(p_aux)->size) / ((double) D_RO(p_aux)->max_size)) * FULL;
}

// Inicializa uma nova tabela hash persistente no pool `pop`.
// Cria o objeto `struct hash`, aloca os arrays `data` e `occupied` e
// define todos os slots como DEFAULT / false.
void start_hash(PMEMobjpool *pop, TOID(struct hash) *p_hash){

  TX_BEGIN(pop){
    TX_ADD_DIRECT(p_hash);

    *p_hash = TX_NEW(struct hash);

    D_RW(*p_hash)->size = 0;
    D_RW(*p_hash)->max_size = INITIAL_SIZE;

    // Controle de tempo de vida (opcional) — decrementa se configurado
    if (lifetime != DEFAULT) {
      if (lifetime == 0)
        exit(0);
      lifetime--;
    }

    // Aloca arrays persistentes para dados e flags de ocupação
    D_RW(*p_hash)->data = TX_ALLOC(int, sizeof(int) * D_RO(*p_hash)->max_size);
    D_RW(*p_hash)->occupied = TX_ALLOC(char, sizeof(char) * D_RO(*p_hash)->max_size);

    // Inicializa slots
    for (int i = 0; i < D_RO(*p_hash)->max_size; i++){
      D_RW(D_RW(*p_hash)->data)[i] = DEFAULT;
      D_RW(D_RW(*p_hash)->occupied)[i] = false;
    }
  } TX_END

}

// Expande a tabela hash para `new_size` quando a taxa de ocupação ultrapassa
// EXPAND_RATE. Reinsere elementos no novo array usando a função de hash
// e verificação linear para resolver colisões.
void expand_hash(PMEMobjpool *pop, TOID(struct hash) p_aux) {

  if (!TOID_IS_NULL(p_aux) && (hash_rate(p_aux) >= EXPAND_RATE)) {

    int old_size = D_RO(p_aux)->max_size;
    int new_size = D_RO(p_aux)->max_size * SIZE_RATE;

    TX_BEGIN(pop) {

      TX_ADD(p_aux);

      // Aloca novo espaço persistente para dados e flags
      TOID(int) new_data = TX_ALLOC(int, sizeof(int) * new_size);
      TOID(char) new_occupied = TX_ZALLOC(char, sizeof(char) * new_size);

      if (!TOID_IS_NULL(new_data)) {
        // Garante que todos os novos slots partam de DEFAULT
        for (int i = 0; i < new_size; i++) {
          D_RW(new_data)[i] = DEFAULT;
        }
      }
  
      // Se ambas alocações foram bem sucedidas, re-hash todos os itens
      if (!TOID_IS_NULL(new_data) && !TOID_IS_NULL(new_occupied)) {
  
        for (int position = 0; position < D_RO(p_aux)->max_size; position++) {
  
          // Move apenas os slots que estão marcados como ocupados
          if (D_RO(D_RW(p_aux)->occupied)[position]) {
            int new_position = hash_function(D_RO(D_RW(p_aux)->data)[position],new_size);

            // Verificação linear no novo array até encontrar um slot livre
            while(D_RO(new_occupied)[new_position]) {
              new_position++;
              new_position = new_position % new_size;
              if (new_position == hash_function(D_RO(D_RW(p_aux)->data)[position],new_size))
                break;
            }
            D_RW(new_data)[new_position] = D_RO(D_RW(p_aux)->data)[position];
            D_RW(new_occupied)[new_position] = true;
          }
  
        }
  
        // Libera as antigas estruturas e substitui pelas novas
        TX_FREE(D_RW(p_aux)->data);
        TX_FREE(D_RW(p_aux)->occupied);
  
        D_RW(p_aux)->max_size = new_size;
        D_RW(p_aux)->data = new_data;
        D_RW(p_aux)->occupied = new_occupied;
      }
    } TX_END
  }
}

// Reduz a tabela hash quando a taxa de ocupação cair abaixo de REDUCTION_RATE
// Re-hash para um array menor e transfere somente os elementos ativos.
void reduce_hash(PMEMobjpool *pop, TOID(struct hash) p_aux) {

  if (!TOID_IS_NULL(p_aux) && (hash_rate(p_aux) < REDUCTION_RATE) && (D_RO(p_aux)->max_size > INITIAL_SIZE)) {

    int old_size = D_RO(p_aux)->max_size;
    int new_size = D_RO(p_aux)->max_size / SIZE_RATE;

    TX_BEGIN(pop) {
      
      TX_ADD(p_aux);
  
      // Aloca o novo array reduzido
      TOID(int) new_data = TX_ALLOC(int, sizeof(int) * new_size);
      TOID(char) new_occupied = TX_ZALLOC(char, sizeof(char) * new_size);

      if (!TOID_IS_NULL(new_data)) {
        for (int i = 0; i < new_size; i++) {
          D_RW(new_data)[i] = DEFAULT;
        }
      }

      if (!TOID_IS_NULL(new_data) && !TOID_IS_NULL(new_occupied)) {
  
        for (int position = 0; position < D_RO(p_aux)->max_size; position++) {
  
          if (D_RO(D_RW(p_aux)->occupied)[position]) {
            int new_position = hash_function(D_RO(D_RW(p_aux)->data)[position],new_size);

            while(D_RO(new_occupied)[new_position]) {
              new_position++;
              new_position = new_position % new_size;
              if (new_position == hash_function(D_RO(D_RW(p_aux)->data)[position],new_size))
                break;
            }
            D_RW(new_data)[new_position] = D_RO(D_RW(p_aux)->data)[position];
            D_RW(new_occupied)[new_position] = true;
          }
        }
        
        // Substitui os arrays antigos pelos novos
        TX_FREE(D_RW(p_aux)->data);
        TX_FREE(D_RW(p_aux)->occupied);
  
        D_RW(p_aux)->max_size = new_size;
        D_RW(p_aux)->data = new_data;
        D_RW(p_aux)->occupied = new_occupied;
      }
    } TX_END
  }
}

// Insere um valor `dado` na tabela usando verificação linear.
// Retorna true em sucesso e false se a tabela estiver cheia.
char insert (PMEMobjpool *pop, TOID(struct hash) p_aux, int dado){

  if (hash_rate(p_aux) >= FULL) {
    printf("Hash cheio.\n");
    return false;
  }
  
  // Garante espaço suficiente antes de inserir
  expand_hash(pop, p_aux);

  TX_BEGIN(pop) {

    TX_ADD(p_aux);
    // Marca a região de dados como parte da transação para persistência
    pmemobj_tx_add_range_direct(D_RW(D_RW(p_aux)->data), sizeof(int) * D_RO(p_aux)->max_size);
    pmemobj_tx_add_range_direct(D_RW(D_RW(p_aux)->occupied), sizeof(char) * D_RO(p_aux)->max_size);

    int posicao = hash_function(dado, D_RO(p_aux)->max_size);
    
    // Verificação linear até encontrar um slot livre
    while (D_RO(D_RW(p_aux)->occupied)[posicao]){
      posicao++;
      posicao = posicao % D_RO(p_aux)->max_size;
    }
    
    // Controle de tempo de vida (opcional)
    if (lifetime != DEFAULT) {
      if (lifetime == 0)
        exit(0);
      lifetime--;
    }
  
    D_RW(D_RW(p_aux)->data)[posicao] = dado;
    D_RW(D_RW(p_aux)->occupied)[posicao] = true;
    D_RW(p_aux)->size++;
  } TX_END
  
  return true;
}

// Busca a posição de `dado` na tabela. Retorna o índice (0-based) se encontrado
// ou DEFAULT caso não exista. A busca termina ao encontrar um slot DEFAULT
// (assumindo que itens além dele não podem pertencer à mesma sequência de verificação)
int search_value (TOID(struct hash) p_aux, int dado){

  if (hash_rate(p_aux) <= EMPTY) {
    printf("Hash vazio.\n");
    return DEFAULT;
  }

  int posicao = hash_function(dado, D_RO(p_aux)->max_size);

  for (int i = 0; D_RO(D_RW(p_aux)->data)[posicao] != DEFAULT; i++){
      
    if (D_RO(D_RW(p_aux)->data)[posicao] == dado && D_RO(D_RW(p_aux)->occupied)[posicao]){
      return posicao;
    }

    posicao++;
    posicao = posicao % D_RO(p_aux)->max_size;

    if (posicao == hash_function(dado, D_RO(p_aux)->max_size))
      break;
  }

  return DEFAULT;
}

// Remove o elemento na posição (1-based no UI) fornecida pelo usuário.
// Marca o slot como não-ocupado sem limpar o valor numérico (flag tombstone).
char remove_position (PMEMobjpool *pop, TOID(struct hash) p_aux, int posicao){

  if (hash_rate(p_aux) <= EMPTY) {
    printf("Hash vazio.\n");
    return false;
  }

  // Entrada do usuário é 1-based
  if (posicao < 1 || posicao > D_RO(p_aux)->max_size){
    printf("Posição invalida.\n");
    return false;
  }

  posicao--;

  if (!D_RO(D_RW(p_aux)->occupied)[posicao]){
    printf("Posição vazia.\n");
    return false;
  }
  

  TX_BEGIN(pop) {
    TX_ADD(p_aux);
    pmemobj_tx_add_range_direct(D_RW(D_RW(p_aux)->occupied), sizeof(char) * D_RO(p_aux)->max_size);

    D_RW(D_RW(p_aux)->occupied)[posicao] = false;
    
    if (lifetime != DEFAULT) {
      if (lifetime == 0)
        exit(0);
      lifetime--;
    }

    D_RW(p_aux)->size--;
  } TX_END

  // Verifica se é possível reduzir a tabela após remoção
  reduce_hash(pop, p_aux);
  return true;
}

// Remove todas as ocorrências do valor `dado` marcando os slots como tombstones.
// Retorna true se ao menos um elemento foi removido.
char remove_value (PMEMobjpool *pop, TOID(struct hash) p_aux, int dado){

  char removed = false;
  
  if (hash_rate(p_aux) <= EMPTY) {
    printf("Hash vazio.\n");
    return removed;
  }
  
  TX_BEGIN(pop) {

    TX_ADD(p_aux);
    pmemobj_tx_add_range_direct(D_RW(D_RW(p_aux)->occupied), sizeof(char) * D_RO(p_aux)->max_size);

    int posicao = hash_function(dado, D_RO(p_aux)->max_size);
    while (D_RO(D_RW(p_aux)->data)[posicao] != DEFAULT){

      if (D_RO(D_RW(p_aux)->data)[posicao] == dado && D_RO(D_RW(p_aux)->occupied)[posicao]){
        D_RW(D_RW(p_aux)->occupied)[posicao] = false;

        if (lifetime != DEFAULT) {
          if (lifetime == 0)
            exit(0);
          lifetime--;
        }

        D_RW(p_aux)->size--;
        removed = true;
      }
      posicao++;
      posicao = posicao % D_RO(p_aux)->max_size;
      
      if (posicao == hash_function(dado, D_RO(p_aux)->max_size))
        break;
    }
  } TX_END

  // Após remoções, tenta reduzir a tabela enquanto for possível
  while ((hash_rate(p_aux) < REDUCTION_RATE) && (D_RO(p_aux)->max_size > INITIAL_SIZE)) {

    int last_size = D_RO(p_aux)->max_size;

    reduce_hash(pop, p_aux);

    if (D_RO(p_aux)->max_size == last_size)
      break;
  }

  return removed;
}

// Restaura (reativa) o slot em `posicao` que ainda contém um valor numérico
// mas está marcado como removido (tombstone). Entrada é 1-based.
char restore_position (PMEMobjpool *pop, TOID(struct hash) p_aux, int posicao){

  if (posicao < 1 || posicao > D_RO(p_aux)->max_size){
    printf("Posição invalida.\n");
    return false;
  }

  posicao--;

  if (D_RO(D_RW(p_aux)->data)[posicao] == DEFAULT){
    printf("Posição vazia.\n");
    return false;
  }
  
  // Garante espaço antes de marcar ocupado
  expand_hash(pop, p_aux);

  TX_BEGIN(pop) {
    TX_ADD(p_aux);
    pmemobj_tx_add_range_direct(D_RW(D_RW(p_aux)->occupied), sizeof(char) * D_RO(p_aux)->max_size);
    D_RW(D_RW(p_aux)->occupied)[posicao] = true;

    if (lifetime != DEFAULT) {
      if (lifetime == 0)
        exit(0);
      lifetime--;
    }

    D_RW(p_aux)->size++;
  } TX_END

  return true;
}

// Reseta a tabela hash: libera estruturas persistentes atuais e cria uma nova
void reset_hash(PMEMobjpool *pop, struct my_root * root) {

  TX_BEGIN (pop) {

    TX_ADD_DIRECT(&root->p_hash);
    
    TX_FREE(D_RW(root->p_hash)->occupied);
    TX_FREE(D_RW(root->p_hash)->data);
    
    if (lifetime != DEFAULT) {
      if (lifetime == 0)
      exit(0);
      lifetime--;
    }
  
    TX_FREE(root->p_hash);
    root->p_hash = TOID_NULL(struct hash);
    start_hash(pop, &root->p_hash);

  } TX_END

}

int main(int argc, char *argv[]) {

  #ifdef MASSIVE_TEST

    #ifdef _WIN32
      const char *null_device = "NUL";
    #else
      const char *null_device = "/dev/null";
    #endif

    if (freopen(null_device, "w", stdout) == NULL) {
      perror("Erro ao redirecionar stdout");
      return 1;
    }
  #endif
  
  switch (argc) {
    case 1: break;
    case 2: lifetime = atoi(argv[1]); break;
    case 3:
      lifetime = atoi(argv[1]);

      strcpy(pool_name, POOL_NAME);
      if (strlen(argv[2]) > (BUFFER_SIZE - strlen(pool_name) - 5))
        argv[2][BUFFER_SIZE - strlen(pool_name) - 5] = '\0';

      strcat(pool_name, argv[2]);
      break;
    default:
      perror("Try to use less arguments.\n");
      return 1;
  }

  PMEMobjpool *pop = pmemobj_create(strcat(pool_name, ".obj"), LAYOUT_NAME, POOL_SIZE, 0666);
  if (pop == NULL) {
    /* Abre o pool existente e retorna um ponteiro para o pool */
      pop = pmemobj_open(pool_name, LAYOUT_NAME);
      if (pop == NULL) {
        perror("pmemobj_open\n");
        return 1;
      }
  }

/* Obtém um ponteiro convencional para o objeto root (raiz) do pool */
  struct my_root *root = D_RW(POBJ_ROOT(pop, struct my_root));

  if (TOID_IS_NULL(root->p_hash)){
    start_hash(pop, &root->p_hash);
  }

  int option, data;
  char buffer[BUFFER_SIZE];

  while (true) {
    display(root->p_hash, stdout);

    if (lifetime <0) {
      printf("\nEnter your choice:\n1. Insert data\n2. Remove by position\n3. Remove by value\n4. Search by value\n5. Restore by position\n6. Export Hash\n7. Reset Hash\n8. Exit\n >> ");
    } else {
      printf("\nEnter your choice: (");
      switch (lifetime) {
        case 0: printf("- - -"); break;
        case 1: printf("█ - -"); break;
        case 2: printf("█ █ -"); break;
        case 3: printf("█ █ █"); break;
        default: printf("%dx █",lifetime);
      }
      printf(")\n1. Insert data\n2. Remove by position\n3. Remove by value\n4. Search by value\n5. Restore by position\n6. Export Hash\n7. Reset Hash\n8. Exit\n >> ");
    }
    fgets(buffer, BUFFER_SIZE-1, stdin);
    option = atoi(buffer);

    // Alterna entre as possíveis opções do menu
    switch (option) {

      case 1: // Caso da inserção
        printf("Enter data to be inserted: ");
        fgets(buffer, BUFFER_SIZE-1, stdin);
        data = atoi(buffer);

        if (insert(pop, root->p_hash, data)) {
          printf("Data inserted!\n");
        } else {
          printf("Data not inserted.\n");
        }
        break;

      case 2: // Caso da remoção posicional
        printf("Enter position to be removed: ");
        fgets(buffer, BUFFER_SIZE-1, stdin);
        data = atoi(buffer);

        if (remove_position(pop, root->p_hash, data)) {
          printf("Position removed!\n");
        } else {
          printf("Position not removed.\n");
        }
        break;

      case 3: // Caso da remoção de itens
        printf("Enter value to be removed: ");
        fgets(buffer, BUFFER_SIZE-1, stdin);
        data = atoi(buffer);

        if (remove_value(pop, root->p_hash, data)) {
          printf("Data removed!\n");
        } else {
          printf("Data not removed.\n");
        }
        break;

      case 4: // Caso da busca (para ver se há um dado item)
        printf("Enter value to be searched: ");
        fgets(buffer, BUFFER_SIZE-1, stdin);
        data = atoi(buffer);

        if ((data = search_value(root->p_hash, data)) >= 0) {
          printf("Data found at position %d.\n", data+1);
        } else {
          printf("No data found.\n");
        }
        break;

      case 5: // Caso de restauração de itens recém-deletados
        printf("Enter position to be restored: ");
        fgets(buffer, BUFFER_SIZE-1, stdin);
        data = atoi(buffer);

        if (restore_position(pop, root->p_hash, data)) {
          printf("Position restored!\n");
        } else {
          printf("Position not restored.\n");
        }
        break;
        
      case 6: // Caso de exportação da hash
        printf("Enter file name to be added: ");
        fgets(buffer, BUFFER_SIZE-1, stdin);

        buffer[strcspn(buffer, "\n")] = '\0';
        buffer[BUFFER_SIZE-5] = '\0';

        if (isalpha(buffer[0])) {
          FILE* output_file = fopen(strcat(buffer,".txt"),"a+");
          display(root->p_hash, output_file);
          fclose(output_file);
          printf("Hash exported!\n");
        } else {
          printf("Failed to export Hash!\n");
        }
        break;

      case 7: // Caso de reset da hash
        reset_hash(pop, root);
        printf("Hash reseted!\n");
        break;

      case 8: // Caso de saída do programa
        pmemobj_close(pop);
        exit(0);
      
      default:
        printf("No command found.\n");
    }
  }
  pmemobj_close(pop);
  return 0;
  
}
