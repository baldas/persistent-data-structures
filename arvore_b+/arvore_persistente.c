// Árvore B+ persistente em C usando libpmemobj (Kit de Desenvolvimento de Memória Persistente - PMDK)
// Este arquivo implementa uma árvore B+ em memória persistente, organizada como
// uma estrutura de índices com acesso por faixas do array persistente. A árvore
// é usada para localizar intervalos de valores em um vetor ordenado, reduzindo a
// busca linear ao longo da estrutura de índices.
//
// O projeto mantém o estado principal em objetos PMDK e usa transações
// (`TX_BEGIN` / `TX_END`) para garantir consistência persistente em um pool
// persistente do tipo libpmemobj.

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <limits.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

// Inclusão da biblioteca para programação em memória persistente (PMDK)
#include <libpmemobj.h>

// Configurações de buffers e da árvore B+
#define BUFFER_SIZE 64
#define DEFAULT INT_MIN
#define INITIAL_SIZE 8
#define SIZE_RATE 2
#define EXPAND_RATE 0.75
#define REDUCTION_RATE (EXPAND_RATE / SIZE_RATE)

// Configurações do pool persistente
#define LAYOUT_NAME "BTREEPLUS"
#define KB 1024ULL
#define MB (1024ULL * KB)
#define GB (1024ULL * MB)
#define POOL_SIZE PMEMOBJ_MIN_POOL
#define POOL_NAME "tree_plus_pool"

// Inicialização de variáveis e do pool
int lifetime = DEFAULT;
char pool_name[BUFFER_SIZE] = "default";

POBJ_LAYOUT_BEGIN(BTREEPLUS);
  POBJ_LAYOUT_ROOT(BTREEPLUS, struct my_root);
  POBJ_LAYOUT_TOID(BTREEPLUS, struct array);
  POBJ_LAYOUT_TOID(BTREEPLUS, int);
POBJ_LAYOUT_END(BTREEPLUS);

// Definição das estruturas de dados persistentes.
// `struct array` mantém o estado do array persistente armazenado no pool.
// - size: número de elementos atualmente ocupados
// - max_size: capacidade atual do array
// - data: array persistente de inteiros armazenados
struct array {
  int size;
  int max_size;
  TOID(int) data;
};

typedef struct array Array;

// `my_root` é o root object do pool PMEM e aponta para o array persistente
// que guarda os dados da árvore B+.
struct my_root {
  TOID(struct array) p_array;
};

// `B_tree` representa um nó da árvore de índices B+.
// - value: valor usado para particionar o intervalo de dados
// - left/right: subárvores esquerda e direita
// - array_left/array_right: faixa do array associada ao nó
//
// A árvore não armazena todos os dados diretamente; ela organiza índices que
// apontam para blocos do array persistente.
typedef struct b_tree {
    int value;
    struct b_tree* left;
    struct b_tree* right;
    int array_left;
    int array_right;
} B_tree;

// `B_tree_plus` é o conjunto formado pela árvore de índices e pelo array
// persistente que contém os dados reais.
typedef struct {
    struct b_tree* index;
    TOID(struct array) content;
} B_tree_plus;

// Inicializa a árvore de índices em memória volátil.
// A subárvore é criada com um nó raiz contendo `DEFAULT` e com faixas vazias
// inicializadas para o primeiro bloco de dados.
bool init_b_tree(B_tree** root) {

    if ((*root) != NULL) {
        free(*root);
    }

    (*root) = malloc(sizeof(B_tree));
    (*root)->value = DEFAULT;
    (*root)->left = NULL;
    (*root)->right = NULL;
    (*root)->array_left = 0;
    (*root)->array_right = INITIAL_SIZE/SIZE_RATE;

    return true;
}

// Cria o array persistente que armazenará os dados da árvore B+.
// Cada slot do array começa com o valor `DEFAULT`, indicando ausência de dado.
// A estrutura também mantém o controle do tamanho atual e da capacidade total.
bool init_array(PMEMobjpool *pop, TOID(struct array) *p_array) {

    TX_BEGIN(pop) {
        TX_ADD_DIRECT(p_array);

        *p_array = TX_NEW(struct array);

        D_RW(*p_array)->size = 0;
        D_RW(*p_array)->max_size = INITIAL_SIZE;
        
        if (lifetime != DEFAULT) {
            if (lifetime == 0)
                exit(0);
            lifetime--;
        }

        D_RW(*p_array)->data = TX_ALLOC(int, sizeof(int) * D_RO(*p_array)->max_size);

        for (int i = 0; i < D_RO(*p_array)->max_size; i++) {
            D_RW(D_RW(*p_array)->data)[i] = DEFAULT;
        }

    } TX_END

    return true;
}

// Libera recursivamente todos os nós da árvore de índices em memória volátil.
// A função percorre a árvore em pós-ordem para remover os filhos antes do pai.
bool delete_b_tree(B_tree** root) {

    if ((*root) != NULL) {
        delete_b_tree(&((*root)->left));
        delete_b_tree(&((*root)->right));
        free(*root);
    }

    (*root) = NULL;

    return true;
}

// Recria o array persistente removendo o objeto antigo e inicializando um novo.
// Esse comportamento é usado quando o usuário solicita reset do conjunto B+.
bool reset_array(PMEMobjpool *pop, TOID(struct array) *array) {

    if (TOID_IS_NULL(*array)) {
        return false;
    }

    TX_BEGIN(pop) {
        TX_ADD_DIRECT(array);

        if (!TOID_IS_NULL(D_RO(*array)->data)) {
            TX_FREE(D_RW(*array)->data);
        }

        if (lifetime != DEFAULT) {
            if (lifetime == 0)
                exit(0);
            lifetime--;
        }

        TX_FREE(*array);
        *array = TOID_NULL(struct array);
        
    } TX_END
    
    init_array(pop, array);

    return true;
}

// Reinicializa a árvore de índices para um estado base sem remover a estrutura
// de dados persistente.
bool reset_b_tree(B_tree** root) {
    return delete_b_tree(root) && init_b_tree(root);
}

// Calcula a altura da árvore de índices, usada para comparar a altura da árvore
// com o tamanho do array persistente durante expansões e reduções.
int get_height(B_tree* root) {
    if (root == NULL) {
        return 0;
    }

    int left_height = get_height(root->left);
    int right_height = get_height(root->right);

    if (left_height > right_height) {
        return left_height + 1;
    } else {
        return right_height + 1;
    }
}

// Cria uma subárvore vazia com altura `height`.
// Os nós internos recebem `DEFAULT` em valor e intervalos sem associação.
B_tree* new_subtree(B_tree** root, int height) {

    if (height <= 0) {
        return NULL;
    }

    *root = malloc(sizeof(B_tree));
    (*root)->value = DEFAULT;
    (*root)->left = new_subtree(&((*root)->left), height-1);
    (*root)->right = new_subtree(&((*root)->right), height-1);
    (*root)->array_left = DEFAULT;
    (*root)->array_right = DEFAULT;
    return *root;
}

// Atualiza os valores e intervalos de cada nó a partir do array persistente.
// `init_chunk` e `end_chunk` delimitam a faixa do array que cada subárvore
// representa.
void update_node(B_tree* node, int init_chunk, int end_chunk, TOID(struct array) array) {
    if (node == NULL) {
        return;
    }

    int first_chunk = init_chunk;
    int second_chunk = (end_chunk - init_chunk) / 2 + init_chunk;

    node->value = D_RO(D_RO(array)->data)[second_chunk];
    node->array_left = first_chunk;
    node->array_right = second_chunk;
    update_node(node->left, first_chunk, second_chunk, array);
    update_node(node->right, second_chunk, end_chunk, array);
}

// Reconcilia a árvore de índices com o tamanho e conteúdo do array persistente.
// Se a árvore estiver maior/menor do que o volume de dados, ela é expandida ou
// reduzida para manter as faixas dos nós consistentes com o array.
bool update_b_tree(B_tree_plus* b_tree_plus) {

    int height_tree = get_height(b_tree_plus->index);
    int height_array = D_RO(b_tree_plus->content)->max_size / INITIAL_SIZE;
    int temp = 0;
    while (height_array > 1) {
        height_array /= SIZE_RATE;
        temp++;
    }
    height_array += temp;


    if (height_tree != height_array) {

        if (height_tree > height_array){

            // Reduz a árvore para o tamanho do array, removendo os nós da diagonal direita
            while (height_tree > height_array) {
                delete_b_tree(&(b_tree_plus->index->right));
                B_tree* temp = b_tree_plus->index->left;
                free(b_tree_plus->index);
                b_tree_plus->index = temp;
                height_tree--;
            }

        } else {

            // Expande a árvore para o tamanho do array, adicionando nós à diagonal direita
            while (height_tree < height_array) {
                struct b_tree * new_root = malloc(sizeof(B_tree));
                new_root->value = D_RO(D_RO(b_tree_plus->content)->data)[b_tree_plus->index->array_right * SIZE_RATE];
                new_root->left = b_tree_plus->index;
                new_subtree(&(new_root->right), height_tree);
                new_root->array_left = 0;
                new_root->array_right = b_tree_plus->index->array_right * SIZE_RATE;
                b_tree_plus->index = new_root;
                height_tree++;
            }

        }
    }
    
    //Atualizar dados da árvore com os valores do array
    update_node(b_tree_plus->index, 0, D_RO(b_tree_plus->content)->max_size, b_tree_plus->content);

    return true;
}

// Expande o array persistente quando a taxa de ocupação atingir o limite
// configurado em `EXPAND_RATE`.
// O redimensionamento preserva os dados anteriores e acrescenta slots vazios.
bool expand_array(PMEMobjpool *pop, TOID(struct array) array) {
    if (TOID_IS_NULL(array)) {
        return false;
    }

    float actual_rate = ((float) D_RO(array)->size) / ((float)D_RO(array)->max_size);

    if (actual_rate < EXPAND_RATE) {
        return false;
    }

    int* temp = malloc(SIZE_RATE * D_RO(array)->max_size * sizeof(int));

    for (int i = 0; i < D_RO(array)->max_size; i++) {
        temp[i] = D_RO(D_RO(array)->data)[i];
    }

    for (int i = D_RO(array)->max_size; i < D_RO(array)->max_size * SIZE_RATE; i++) {
        temp[i] = DEFAULT;
    }

    TX_BEGIN(pop) {
        TX_ADD(array);

        TOID(int) new_data = TX_ALLOC(int, sizeof(int) * D_RO(array)->max_size * SIZE_RATE);
        pmemobj_memcpy(pop, D_RW(new_data), temp, sizeof(int) * D_RO(array)->max_size * SIZE_RATE, POBJ_FLAG_ZERO);
        TX_FREE(D_RO(array)->data);

        if (lifetime != DEFAULT) {
            if (lifetime == 0)
                exit(0);
            lifetime--;
        }

        D_RW(array)->data = new_data;
        D_RW(array)->max_size *= SIZE_RATE;
    } TX_END

    return true;
}

// Reduz o array persistente quando a taxa de ocupação cair abaixo de
// `REDUCTION_RATE`, preservando apenas o espaço necessário.
bool reduce_array(PMEMobjpool *pop, TOID(struct array) array) {

    if (TOID_IS_NULL(array)) {
        return false;
    }

    float actual_rate = ((float) D_RO(array)->size) / ((float)D_RO(array)->max_size);

    if (actual_rate >= REDUCTION_RATE || D_RO(array)->max_size / SIZE_RATE <= INITIAL_SIZE) {
        return false;
    }

    int* temp = malloc((D_RO(array)->max_size/SIZE_RATE) * sizeof(int));

    for (int i = 0; i < D_RO(array)->max_size/SIZE_RATE; i++) {
        temp[i] = D_RO(D_RO(array)->data)[i];
    }

    TX_BEGIN(pop) {
        TX_ADD(array);

        TOID(int) new_data = TX_ALLOC(int, sizeof(int) * D_RO(array)->max_size / SIZE_RATE);
        pmemobj_memcpy(pop, D_RW(new_data), temp, sizeof(int) * D_RO(array)->max_size / SIZE_RATE, POBJ_FLAG_ZERO);
        TX_FREE(D_RO(array)->data);

        if (lifetime != DEFAULT) {
            if (lifetime == 0)
                exit(0);
            lifetime--;
        }

        D_RW(array)->data = new_data;
        D_RW(array)->max_size /= SIZE_RATE;
    } TX_END

    return true;
}

// Insere um novo valor no array persistente, mantendo o vetor ordenado.
// A função usa o valor `DEFAULT` como marca de slot vazio e reorganiza os itens
// à medida que a inserção ocorre.
bool insert_data(PMEMobjpool *pop, TOID(struct array) array, int value) {

    expand_array(pop, array);

    if ((D_RO(D_RO(array)->data))[D_RO(array)->max_size-1] != DEFAULT) {
        return false;
    }

    int* temp = malloc(D_RO(array)->max_size * sizeof(int));
    for (int i = 0; i < D_RO(array)->max_size; i++) {
        if (D_RO(D_RO(array)->data)[i] == DEFAULT) {
            temp[i] = value;

            // Copia o array auxiliar para o array persistente, somente até o índice i
            TX_BEGIN(pop) {
                TX_ADD(D_RW(array)->data);
                TX_ADD(array);

                pmemobj_memcpy(pop, D_RW(D_RW(array)->data), temp, (i+1) * sizeof(int), POBJ_FLAG_ZERO);
                if (lifetime != DEFAULT) {
                    if (lifetime == 0)
                        exit(0);
                    lifetime--;
                }
                D_RW(array)->size++;

            } TX_END

            free(temp);
            return true;
        }

        if (D_RO(D_RO(array)->data)[i] < value) {
            temp[i] = D_RO(D_RO(array)->data)[i];
            continue;
        }

        temp[i] = value;
        value = D_RO(D_RO(array)->data)[i];
    }

    free(temp);
    return false;
}

// Remove todas as ocorrências do valor informado do array persistente.
// Após a remoção, a estrutura tenta reduzir o tamanho do array se a taxa de
// ocupação justificar.
bool remove_data(PMEMobjpool *pop, TOID(struct array) array, int value) {

    if (D_RO(D_RO(array)->data)[0] == DEFAULT) {
        return false;
    }

    int* temp = malloc(D_RO(array)->max_size * sizeof(int));
    for (int i = 0; i < D_RO(array)->max_size; i++) {

        if (D_RO(D_RO(array)->data)[i] < value) {
            temp[i] = D_RO(D_RO(array)->data)[i];
            continue;
        }

        if (D_RO(D_RO(array)->data)[i] > value || D_RO(D_RO(array)->data)[i] == DEFAULT) {
            free(temp);
            return false;
        }

        int j = i;
        while (j < D_RO(array)->max_size && D_RO(D_RO(array)->data)[j] == value) {
            j++;
        }

        int new_size = D_RO(array)->size - (j - i);
        while (i < j && j < D_RO(array)->max_size) {
            temp[i] = D_RO(D_RO(array)->data)[j];
            i++;
            j++;
        }

        while (i < D_RO(array)->max_size) {
            temp[i] = DEFAULT;
            i++;
        }

        TX_BEGIN(pop) {
            TX_ADD(D_RW(array)->data);
            TX_ADD(array);

            pmemobj_memcpy(pop, D_RW(D_RW(array)->data), temp, D_RO(array)->max_size * sizeof(int), POBJ_FLAG_ZERO);
            if (lifetime != DEFAULT) {
                if (lifetime == 0)
                    exit(0);
                lifetime--;
            }
            D_RW(array)->size = new_size;
        } TX_END

    }

    free(temp);
    reduce_array(pop, array);
    return true;
}

// Busca a posição do valor informado usando a árvore B+ como índice.
// A árvore aponta para os intervalos relevantes do array; a busca final é feita
// diretamente no vetor persistente dentro da faixa correspondente.
int search_data(B_tree_plus* b_tree_plus, int value) {

    if (D_RO(D_RO(b_tree_plus->content)->data)[0] == DEFAULT) {
        return INT_MIN;
    }

    // busca binária na árvore B+ para encontrar o valor
    B_tree* current_node = b_tree_plus->index;
    while (current_node != NULL) {

        if (current_node->left == NULL && current_node->right == NULL) {
            break;
        }

        if (value < current_node->value) {
            current_node = current_node->left;
            continue;
        }
        if (value > current_node->value) {
            current_node = current_node->right;
            continue;
        }
        return current_node->array_right;
    }

    if (current_node->value > value) {
        for (int i = current_node->array_left; i < current_node->array_right; i++) {
            if (D_RO(D_RO(b_tree_plus->content)->data)[i] == value) {
                return i;
            }
        }
    } else {
        for (int i = current_node->array_right; i < current_node->array_right + INITIAL_SIZE/SIZE_RATE; i++) {
            if (D_RO(D_RO(b_tree_plus->content)->data)[i] == value) {
                return i;
            }
        }
    }

    return INT_MIN;
}

// Exibe o conteúdo do array persistente em blocos de leitura mais amigáveis.
// Locais vazios são marcados como `**` para facilitar a inspeção visual.
bool display_data(TOID(struct array) array, FILE* output_file) {

    int data_chunk = INITIAL_SIZE / SIZE_RATE;

    for (int i = 0; i < D_RO(array)->max_size/data_chunk; i++) {
        
        fprintf(output_file, "Chunk %d: [ ", i+1);

        for (int j = 0; j < data_chunk; j++) {
            if (D_RO(D_RO(array)->data)[i*data_chunk+j] == DEFAULT) {
                fprintf(output_file, "**");
            } else {
                fprintf(output_file,"%d", D_RO(D_RO(array)->data)[i*data_chunk+j]);
            }

            if (j+1 < data_chunk) {
                fprintf(output_file, " ");
            }
        }

        fprintf(output_file, " ]\n");
    }

    return true;
}

// Exibe os nós da árvore de índices e os intervalos de dados associados a cada
// nó, permitindo acompanhar a estrutura hierárquica da B+.
bool display_b_tree(B_tree* root, FILE* output_file) {

    if (root == NULL) {
        return false;
    }

    display_b_tree(root->left, output_file);
    display_b_tree(root->right, output_file);
    if (root->value != DEFAULT) {
        fprintf(output_file, "Node value: %d, Array left: %d, Array right: %d\n", root->value, root->array_left/(INITIAL_SIZE/SIZE_RATE) + 1, root->array_right/(INITIAL_SIZE/SIZE_RATE) + 1);
    } else {
        fprintf(output_file, "Node value: **, Array left: %d, Array right: %d\n", root->array_left/(INITIAL_SIZE/SIZE_RATE) + 1, root->array_right/(INITIAL_SIZE/SIZE_RATE) + 1);
    }

    return true;
}

// Programa principal da árvore B+ persistente.
// O fluxo de execução cria ou abre um pool PMDK, inicializa o array persistente,
// montando a árvore de índices e apresenta um menu interativo para inserção,
// remoção, busca, exportação e reset do conjunto de dados.
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
            perror("failed to open pmemobj pool\n");
            return 1;
        }
    }

    int option, temp;
    char buffer[BUFFER_SIZE];
    B_tree_plus b_tree_plus = {NULL, TOID_NULL(struct array)};
    
    struct my_root *root = D_RW(POBJ_ROOT(pop, struct my_root));
    
    if (TOID_IS_NULL(root->p_array)){
        init_array(pop, &root->p_array);
    }
    b_tree_plus.content = root->p_array;
    
    init_b_tree(&b_tree_plus.index);
    update_b_tree(&b_tree_plus);

    // Menu de interação
	while (true) {
        printf("\n\nCurrent data:\n");
		display_data(b_tree_plus.content, stdout);
        printf("\nCurrent B-Tree:\n");
        display_b_tree(b_tree_plus.index, stdout);

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
        printf("\n1. Insert data\n2. Remove by value\n3. Search by value\n4. Export B-Tree Plus\n5. Reset B-Tree Plus\n6. Exit\n >> ");
		fgets(buffer, BUFFER_SIZE-1, stdin);
        option = atoi(buffer);

        // Alterna entre as possíveis opções do menu
        switch (option) {

            case 1: // Caso da inserção
                printf("Enter data to be inserted: ");
                fgets(buffer, BUFFER_SIZE-1, stdin);

                if (insert_data(pop, b_tree_plus.content, atoi(buffer))) {
                    printf("Data inserted!");
                } else {
                    printf("Data not inserted.");
                }
                update_b_tree(&b_tree_plus);
                break;

            case 2: // Caso da remoção de itens
                printf("Enter value to be removed: ");
                fgets(buffer, BUFFER_SIZE-1, stdin);
                if (remove_data(pop, b_tree_plus.content, atoi(buffer))) {
                    printf("Data removed!");
                } else {
                    printf("Data not removed.");
                }
                update_b_tree(&b_tree_plus);
                break;

            case 3: // Caso da busca (para ver se há um dado item)
                printf("Enter value to be searched: ");
                fgets(buffer, BUFFER_SIZE-1, stdin);
                if ((temp = search_data(&b_tree_plus, atoi(buffer))) > INT_MIN) {
                    printf("Data found at position %d.", temp+1);
                } else {
                    printf("No data found.");
                }
                break;

            case 4: // Caso de exportação da árvore
                printf("Enter file name to be added: ");
                fgets(buffer, BUFFER_SIZE-1, stdin);

                buffer[strcspn(buffer, "\n")] = '\0';
                buffer[BUFFER_SIZE-5] = '\0';

                if (isalpha(buffer[0])) {
                    FILE* output_file = fopen(strcat(buffer,".txt"),"a+");
                    time_t currentTime = time(NULL);

                    fprintf(output_file, "Data Ticket - %s\nCurrent data:\n", ctime(&currentTime));
                    display_data(b_tree_plus.content, output_file);

                    fprintf(output_file, "\nCurrent B-Tree:\n");
                    display_b_tree(b_tree_plus.index, output_file);

                    fprintf(output_file, "\n\nExported by B-Tree Plus program.\n");
                    fprintf(output_file, "----------------------------------------------\n");
                    fclose(output_file);

                    printf("B-Tree Plus exported!\n");
                } else {
                    printf("Failed to export B-Tree Plus!\n");
                }
                break;

            case 5: // Caso de reset da árvore
                reset_array(pop, &root->p_array);
                b_tree_plus.content = root->p_array;
                reset_b_tree(&b_tree_plus.index);
                printf("\nB-Tree Plus reseted!");
                break;

            case 6: // Caso de saída do programa
                delete_b_tree(&b_tree_plus.index);
                exit(0);
            
            default:
        }
	}
	return 0;
}