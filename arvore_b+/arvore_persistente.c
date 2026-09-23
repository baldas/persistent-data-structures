// Árvore B+ persistente em C usando libpmemobj (Kit de Desenvolvimento de Memória Persistente - PMDK)
// Este arquivo implementa uma árvore b+ aberta com busca binária
// e armazenamento persistente via libpmemobj. Comentários adicionados
// para explicar as principais seções e funções do código.

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <limits.h>
#include <string.h>
#include <ctype.h>

// Inclusão da biblioteca para programação em memória persistente (PMDK)
#include <libpmemobj.h>

// Configurações de buffers e da árvore b+
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
  POBJ_LAYOUT_TOID(BTREEPLUS, int);
  POBJ_LAYOUT_TOID(BTREEPLUS, char);
  POBJ_LAYOUT_TOID(BTREEPLUS, struct b_tree_plus);
POBJ_LAYOUT_END(BTREEPLUS);

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

typedef struct array {
    int size;
    int max_size;
    int* data;
} Array;

typedef struct b_tree {
    int value;
    struct b_tree* left;
    struct b_tree* right;
    int array_left;
    int array_right;
} B_tree;

typedef struct {
    struct b_tree* index;
    struct array* content;
} B_tree_plus;

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

bool init_array(Array** data) {

    if ((*data) != NULL) {
        free((*data)->data);
        free(*data);
    }

    (*data) = malloc(sizeof(Array));
    (*data)->size = 0;
    (*data)->max_size = INITIAL_SIZE;
    (*data)->data = malloc(INITIAL_SIZE * sizeof(int));

    for (int i = 0; i < INITIAL_SIZE; i++) {
        (*data)->data[i] = DEFAULT;
    }

    return true;
}

bool delete_b_tree(B_tree** root) {

    if ((*root) != NULL) {
        delete_b_tree(&((*root)->left));
        delete_b_tree(&((*root)->right));
        free(*root);
    }

    (*root) = NULL;

    return true;
}

bool delete_array(Array** array) {

    if ((*array) != NULL) {
        free((*array)->data);
        free(*array);
    }

    (*array) = NULL;

    return true;
}

bool reset_b_tree(B_tree** root) {
    return delete_b_tree(root) && init_b_tree(root);
}

bool reset_array(Array** array) {
    return delete_array(array) && init_array(array);
}

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

void update_node(B_tree* node, int init_chunk, int end_chunk, Array* array) {
    if (node == NULL) {
        return;
    }

    int first_chunk = init_chunk;
    int second_chunk = (end_chunk - init_chunk) / 2 + init_chunk;

    node->value = array->data[second_chunk];
    node->array_left = first_chunk;
    node->array_right = second_chunk;
    update_node(node->left, first_chunk, second_chunk, array);
    update_node(node->right, second_chunk, end_chunk, array);
}

bool update_b_tree(B_tree_plus* b_tree_plus) {

    int height_tree = get_height(b_tree_plus->index);
    int height_array = b_tree_plus->content->max_size / INITIAL_SIZE;
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
                new_root->value = b_tree_plus->content->data[b_tree_plus->index->array_right * SIZE_RATE];
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
    update_node(b_tree_plus->index, 0, b_tree_plus->content->max_size, b_tree_plus->content);

    return true;
}

bool expand_array(Array* array) {
    if (array == NULL) {
        return false;
    }

    float actual_rate = ((float) array->size) / ((float)array->max_size);

    if (actual_rate < EXPAND_RATE) {
        return false;
    }

    int* temp = malloc(SIZE_RATE * array->max_size * sizeof(int));

    for (int i = 0; i < array->max_size; i++) {
        temp[i] = array->data[i];
    }

    for (int i = array->max_size; i < array->max_size * SIZE_RATE; i++) {
        temp[i] = DEFAULT;
    }

    free(array->data);
    array->data = temp;
    array->max_size *= SIZE_RATE;

    return true;
}

bool reduce_array(Array* array) {

    if (array == NULL) {
        return false;
    }

    float actual_rate = ((float) array->size) / ((float)array->max_size);

    if (actual_rate >= REDUCTION_RATE || array->max_size / SIZE_RATE <= INITIAL_SIZE) {
        return false;
    }

    int* temp = malloc((array->max_size/SIZE_RATE) * sizeof(int));

    for (int i = 0; i < array->max_size/SIZE_RATE; i++) {
        temp[i] = array->data[i];
    }

    free(array->data);
    array->data = temp;
    array->max_size /= SIZE_RATE;

    return true;
}

bool insert_data(Array* array, int value) {

    expand_array(array);

    if (array->data[array->max_size-1] != DEFAULT) {
        return false;
    }

    for (int i = 0; i < array->max_size; i++) {
        if (array->data[i] == DEFAULT) {
            array->data[i] = value;
            array->size++;
            return true;
        }

        if (array->data[i] < value) {
            continue;
        }

        if (array->data[i] > value) {
            int temp = array->data[i];
            array->data[i] = value;
            value = temp;
        }

    }

    return false;
}

bool remove_data(Array* array, int value) {

    if (array->data[0] == DEFAULT) {
        return false;
    }

    for (int i = 0; i < array->max_size; i++) {

        if (array->data[i] < value) {
            continue;
        }

        if (array->data[i] > value || array->data[i] == DEFAULT) {
            return false;
        }

        int j = i;
        while (j < array->max_size && array->data[j] == value) {
            j++;
        }

        array->size -= (j - i);

        while (i < j && j < array->max_size) {
            array->data[i] = array->data[j];
            i++;
            j++;
        }

        while (i < array->max_size) {
            array->data[i] = DEFAULT;
            i++;
        }

    }

    reduce_array(array);

    return true;
}

int search_data(B_tree_plus* b_tree_plus, int value) {

    if (b_tree_plus->content->data[0] == DEFAULT) {
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
            if (b_tree_plus->content->data[i] == value) {
                return i;
            }
        }
    } else {
        for (int i = current_node->array_right; i < current_node->array_right + INITIAL_SIZE/SIZE_RATE; i++) {
            if (b_tree_plus->content->data[i] == value) {
                return i;
            }
        }
    }

    return INT_MIN;
}

bool display_data(Array* array, FILE* output_file) {

    int data_chunk = INITIAL_SIZE / SIZE_RATE;

    for (int i = 0; i < array->max_size/data_chunk; i++) {
        
        fprintf(output_file, "Chunk %d: [ ", i+1);

        for (int j = 0; j < data_chunk; j++) {
            if (array->data[i*data_chunk+j] == DEFAULT) {
                fprintf(output_file, "**");
            } else {
                fprintf(output_file,"%d", array->data[i*data_chunk+j]);
            }

            if (j+1 < data_chunk) {
                fprintf(output_file, " ");
            }
        }

        fprintf(output_file, " ]\n");
    }

    return true;
}

bool display_b_tree(B_tree* root, FILE* output_file) {

    if (root == NULL) {
        return false;
    }

    display_b_tree(root->left, output_file);
    display_b_tree(root->right, output_file);
    if (root->value != DEFAULT) {
        fprintf(output_file, "Node value: %d, Array left: %d, Array right: %d\n", root->value, root->array_left, root->array_right);
    } else {
        fprintf(output_file, "Node value: **, Array left: %d, Array right: %d\n", root->array_left, root->array_right);
    }

    return true;
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

    int option, temp;
    char buffer[BUFFER_SIZE];
    B_tree_plus b_tree_plus;
    b_tree_plus.index = NULL;
    b_tree_plus.content = NULL;
    init_array(&b_tree_plus.content);
    init_b_tree(&b_tree_plus.index);

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
        printf("\n1. Insert data\n2. Remove by value\n3. Search by value\n4. Export B-Tree Plus\n5. Reset B-Tree Plus\n6. Exit\n >> ")
		fgets(buffer, BUFFER_SIZE-1, stdin);
        option = atoi(buffer);

        // Alterna entre as possíveis opções do menu
        switch (option) {

            case 1: // Caso da inserção
                printf("Enter data to be inserted: ");
                fgets(buffer, BUFFER_SIZE-1, stdin);

                if (insert_data(b_tree_plus.content, atoi(buffer))) {
                    printf("Data inserted!");
                } else {
                    printf("Data not inserted.");
                }
                update_b_tree(&b_tree_plus);
                break;

            case 2: // Caso da remoção de itens
                printf("Enter value to be removed: ");
                fgets(buffer, BUFFER_SIZE-1, stdin);
                if (remove_data(b_tree_plus.content, atoi(buffer))) {
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
                    display_data(b_tree_plus.content, output_file);
                    display_b_tree(b_tree_plus.index, output_file);
                    fclose(output_file);
                    printf("B-Tree Plus exported!\n");
                } else {
                    printf("Failed to export B-Tree Plus!\n");
                }
                break;

            case 5: // Caso de reset da árvore
                reset_array(&b_tree_plus.content);
                reset_b_tree(&b_tree_plus.index);
                printf("\nB-Tree Plus reseted!");
                break;

            case 6: // Caso de saída do programa
                delete_array(&b_tree_plus.content);
                delete_b_tree(&b_tree_plus.index);
                exit(0);
            
            default:
        }
	}
	return 0;
}