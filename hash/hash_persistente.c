// Hash persistente em C usando libpmemobj (PMDK).
//
// Este módulo implementa uma tabela hash aberta com endereçamento linear
// em memória persistente. A estrutura é mantida em um pool PMDK, de modo que
// o conteúdo e a configuração da tabela sobrevivem a reinicializações do
// processo e podem ser reabertos em execuções posteriores.
//
// A ideia central é manter os arrays de dados e de ocupação no pool, usando
// transações do libpmemobj para garantir consistência em casos de falha e
// implementando o redimensionamento automático conforme a taxa de ocupação.

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <limits.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

// Inclusão da biblioteca para programação em memória persistente (PMDK)
#include <libpmemobj.h>

#define EMPTY 0.0
#define FULL 1.0
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
#define POOL_NAME "/mnt/pmem0/"

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
//
// A função foi desenhada para facilitar visualização em terminal e exportação
// para arquivos de texto, sem alterar o estado da estrutura.

// Exibe o conteúdo atual da tabela em uma representação legível.
// A saída é agrupada em blocos de INITIAL_SIZE posições por linha para que
// o comportamento da sondagem linear e as posições vazias/removidas fiquem
// mais fáceis de acompanhar.
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
// que calcula (dado * dado) % max_size. Pode ser substituída por outra
// estratégia, desde que preserve a propriedade de distribuir os itens pelos
// slots do vetor de forma consistente.
int hash_function(int dado, int max_size) {
     int res = (int) (((long long) dado * dado) % max_size);
    return res < 0 ? res + max_size : res; // Garantia contra resultado negativo
}

// Retorna a taxa de ocupação atual da tabela.
// O valor é usado para decidir expansão e redução automática da estrutura.
double hash_rate(TOID(struct hash) p_aux) {
     return (((double) D_RO(p_aux)->size) / ((double) D_RO(p_aux)->max_size)) * FULL;
}

// Inicializa uma nova tabela hash persistente no pool `pop`.
// Cria o objeto `struct hash`, aloca os arrays `data` e `occupied` e
// define todos os slots como DEFAULT / false. Este passo é executado uma vez
// por raiz do pool, ou quando a estrutura é reiniciada.
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
// e verificação linear para resolver colisões, preservando a ordem lógica dos
// elementos em uma tabela aberta.
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

// Reduz a tabela hash quando a taxa de ocupação cair abaixo de REDUCTION_RATE.
// A operação cria um vetor menor e rehasha apenas os elementos ativos,
// preservando os dados válidos e ajustando a capacidade para a carga atual.
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

// Insere um valor `dado` na tabela utilizando sondagem linear.
// A inserção verifica se há espaço suficiente, realiza expansão quando
// necessário e grava o valor em um slot livre, atualizando também o contador
// de elementos ocupados da estrutura.
bool insert (PMEMobjpool *pop, TOID(struct hash) p_aux, int dado){

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

// Busca a posição de `dado` na tabela. Retorna o índice (0-based) se
// encontrado ou DEFAULT caso não exista. A busca segue a mesma sequência de
// sondagem da inserção e interrompe quando detecta que o percurso completo foi
// realizado, sem perder o comportamento esperado da tabela aberta.
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

// Remove o elemento na posição informada pelo usuário (entrada em formato
// 1-based na interface). A operação marca o slot como vazio em termos de
// ocupação, mas preserva o valor em memória para permitir restauração futura
// por meio de uma marca de tombstone.
bool remove_position (PMEMobjpool *pop, TOID(struct hash) p_aux, int posicao){

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

// Remove todas as ocorrências do valor `dado` marcando os slots como
// tombstones. Essa abordagem reusa a lógica de remoção por ocupação e elimina
// todas as duplicatas encontradas na sequência de sondagem do item.
bool remove_value (PMEMobjpool *pop, TOID(struct hash) p_aux, int dado){

    bool removed = false;
  
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

                if (!removed && lifetime != DEFAULT) {
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
// mas está marcado como removido (tombstone). A entrada é tratada em formato
// 1-based pela interface do programa, e a operação apenas reativa a ocupação do
// slot sem reconstruir a tabela inteira.
bool restore_position (PMEMobjpool *pop, TOID(struct hash) p_aux, int posicao){

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

// Reseta a tabela hash: libera as estruturas persistentes atuais e cria uma
// nova instância vazia da tabela no mesmo pool. É útil para reiniciar o estado
// de execução sem excluir o arquivo ou o pool que mantêm os dados persistentes.
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

// Exporta o conteúdo do array em formato JSON, preservando a informação de
// slots vazios, valores ativos e valores marcados como tombstones.
bool json_content(TOID(struct hash) p_aux, FILE* output_file) {

    for (int i = 0; i < D_RO(p_aux)->max_size; i++) {

        if (D_RO(D_RO(p_aux)->data)[i] == DEFAULT) {
            fprintf(output_file, "\"**\"");
        } else {
          if (D_RO(D_RO(p_aux)->occupied)[i])
            fprintf(output_file,"\"%d\"", D_RO(D_RO(p_aux)->data)[i]);
          else
            fprintf(output_file,"\"!%d\"", D_RO(D_RO(p_aux)->data)[i]);
        }

        if (!(i == D_RO(p_aux)->max_size -1))
            fprintf(output_file,", ");
    }

    return true;
}

// Realiza a exportação dos dados para um arquivo JSON, acrescentando um
// timestamp e o estado atual da carga da estrutura no arquivo gerado.
bool export_json(char * buffer, TOID(struct hash) p_aux) {
    buffer[BUFFER_SIZE-6] = '\0';
    FILE* output_file = fopen(strcat(buffer,".json"),"a+");
    time_t currentTime = time(NULL);

    char buffer_time[BUFFER_SIZE];
    struct tm *info_tempo = localtime(&currentTime);
    strftime(buffer_time, sizeof(buffer_time), "%A, %d/%m/%Y - %H:%M:%S", info_tempo);

    fprintf(output_file, "{\n\"time\":\"%s\",\n", buffer_time);

    fprintf(output_file, "\"charge\": \"%d\",\n",lifetime);

    fprintf(output_file, "\"content\":[");
    json_content(p_aux, output_file); 
    fprintf(output_file, "]\n}\n");

    fclose(output_file);
    return true;
}

// Realiza a exportação dos dados para um arquivo texto, incluindo a data do
// ticket, o estado da carga atual e a representação visual da tabela.
bool export_text(char * buffer, TOID(struct hash) p_aux, int ticket) {
    buffer[BUFFER_SIZE-5] = '\0';
    FILE* output_file = fopen(strcat(buffer,".txt"),"a+");
    time_t currentTime = time(NULL);

    fprintf(output_file, "Data Ticket %d - %s", ticket, ctime(&currentTime));

    if (lifetime >=0) {
        fprintf(output_file, "\nCurrent charge: %dx █\n",lifetime);
    }

    fprintf(output_file, "\nCurrent data:\n");
    display(p_aux, output_file);

    fprintf(output_file, "\n\nExported by Hash program.\n");
    fprintf(output_file, "----------------------------------------------\n");
    fclose(output_file);
    return true;
}

// Programa principal.
//
// O fluxo do menu permite inserir, remover, buscar, resetar, exportar e
// restaurar itens da tabela hash persistente. O pool PMDK é criado ou aberto
// no início da execução e a estrutura raiz do pool é usada para manter a hash
// ao longo do tempo.
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

    int option, data, ticket = 0;
    bool json_export_type = false;
    char buffer[BUFFER_SIZE];

    while (true) {
        display(root->p_hash, stdout);

        printf("\n\nEnter your choice:");
        if (lifetime >=0) {
            printf(" (");
            switch (lifetime) {
                case 0: printf("- - -"); break;
                case 1: printf("█ - -"); break;
                case 2: printf("█ █ -"); break;
                case 3: printf("█ █ █"); break;
                default: printf("%dx █",lifetime);
            }
            printf(")");
        }
        printf("\n1. Insert data\n2. Remove data\n3. Search data\n4. Advanced\n5. Exit\n >> ");
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

            case 2: // Caso da remoção de itens
                printf("Enter value to be removed: ");
                fgets(buffer, BUFFER_SIZE-1, stdin);
                data = atoi(buffer);

                if (remove_value(pop, root->p_hash, data)) {
                    printf("Data removed!\n");
                } else {
                    printf("Data not removed.\n");
                }
                break;

            case 3: // Caso da busca (para ver se há um dado item)
                printf("Enter value to be searched: ");
                fgets(buffer, BUFFER_SIZE-1, stdin);
                data = atoi(buffer);

                if ((data = search_value(root->p_hash, data)) >= 0) {
                    printf("Data found at position %d.\n", data+1);
                } else {
                    printf("No data found.\n");
                }
                break;

            case 4: //Segundo menu com opções avançadas
                    
                printf("\n\nEnter your choice:");
                printf("\n1. Export data\n2. Reset structure\n3. Charge Energy\n4. Cancel Energy Mode\n5. Remove by position\n6. Restore by position\n0. Back\n >> ");
                fgets(buffer, BUFFER_SIZE-1, stdin);
                option = atoi(buffer);

                switch (option) {

                case 1: // Caso de exportação dos dados

                    printf("\nEnter file type to be exported: ");
                    printf("\n1. TXT\n2. JSON\n >> ");
                    fgets(buffer, BUFFER_SIZE-1, stdin);
                    option = atoi(buffer);

                    if (option != 1 && option != 2)
                        break;

                    json_export_type = (option == 2);

                    printf("Enter file name to be added: ");
                    fgets(buffer, BUFFER_SIZE-1, stdin);

                    if (isalpha(buffer[0])) {

                        buffer[strcspn(buffer, "\n")] = '\0';

                        if (json_export_type)
                            export_json(buffer, root->p_hash);
                        else
                            export_text(buffer, root->p_hash, ++ticket);

                        printf("Hash exported!\n");

                    } else
                        printf("Failed to export Hash!\n");
                    break;

                case 2: // Caso de reset da estrutura
                    reset_hash(pop, root);
                    printf("Hash reseted!\n");
                    break;

                case 3: // Caso de adição de energia
                    printf("Enter the amount of energy to be added: ");
                    fgets(buffer, BUFFER_SIZE-1, stdin);
                    if (lifetime != DEFAULT) {
                        lifetime += atoi(buffer);
                    } else {
                        lifetime = atoi(buffer);
                    }
                    printf("Energy added!\n");
                    break;
                
                case 4: // Caso de cancelamento do modo de energia
                    lifetime = DEFAULT;
                    printf("Energy removed!\n");
                    break;
                
                case 5: // Caso de Remoção por Posição
                    printf("Enter position to be removed: ");
                    fgets(buffer, BUFFER_SIZE-1, stdin);
                    data = atoi(buffer);

                    if (remove_position(pop, root->p_hash, data)) {
                        printf("Position removed!\n");
                    } else {
                        printf("Position not removed.\n");
                    }
                    break;

                case 6: // Caso de Restauração por Posição
                    printf("Enter position to be restored: ");
                    fgets(buffer, BUFFER_SIZE-1, stdin);
                    data = atoi(buffer);

                    if (restore_position(pop, root->p_hash, data)) {
                        printf("Position restored!\n");
                    } else {
                        printf("Position not restored.\n");
                    }
                    break;

                default:
                }break;

            case 5: // Caso de saída do programa
                pmemobj_close(pop);
                exit(0);
                break;

            case 41: //Opção secreta para exportar em json com 1 comando
                fgets(buffer, BUFFER_SIZE-1, stdin);
                buffer[strcspn(buffer, "\n")] = '\0';
                export_json(buffer, root->p_hash);
                break;
            
            default:
        }
    }
    pmemobj_close(pop);
    return 0;
}
