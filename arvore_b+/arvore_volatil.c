#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <limits.h>

#define BUFFER_SIZE 64
#define DEFAULT INT_MIN

#define DATA_SIZE 8
#define SIZE_RATE 2
#define EXPAND_RATE 0.75
#define REDUCTION_RATE (EXPAND_RATE / SIZE_RATE)

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
    (*root)->array_right = DATA_SIZE/SIZE_RATE;

    return true;
}

bool init_array(Array** data) {

    if ((*data) != NULL) {
        free((*data)->data);
        free(*data);
    }

    (*data) = malloc(sizeof(Array));
    (*data)->size = 0;
    (*data)->max_size = DATA_SIZE;
    (*data)->data = malloc(DATA_SIZE * sizeof(int));

    for (int i = 0; i < DATA_SIZE; i++) {
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
    int height_array = b_tree_plus->content->max_size / DATA_SIZE;
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

    if (actual_rate >= REDUCTION_RATE || array->max_size / SIZE_RATE <= DATA_SIZE) {
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
        for (int i = current_node->array_right; i < current_node->array_right + DATA_SIZE/SIZE_RATE; i++) {
            if (b_tree_plus->content->data[i] == value) {
                return i;
            }
        }
    }

    return INT_MIN;
}

bool display_data(Array* array) {

    int data_chunk = DATA_SIZE / SIZE_RATE;

    for (int i = 0; i < array->max_size/data_chunk; i++) {
        
        printf("Chunk %d: [ ", i+1);

        for (int j = 0; j < data_chunk; j++) {
            if (array->data[i*data_chunk+j] == DEFAULT) {
                printf("**");
            } else {
                printf("%d", array->data[i*data_chunk+j]);
            }

            if (j+1 < data_chunk) {
                printf(" ");
            }
        }

        printf(" ]\n");
    }

    return true;
}

bool display_b_tree(B_tree* root) {

    if (root == NULL) {
        return false;
    }

    if (root->value != DEFAULT) {
        printf("Node value: %d, Array left: %d, Array right: %d\n", root->value, root->array_left, root->array_right);
    } else {
        printf("Node value: **, Array left: %d, Array right: %d\n", root->array_left, root->array_right);
    }
    display_b_tree(root->left);
    display_b_tree(root->right);

    return true;
}

int main() {

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
		display_data(b_tree_plus.content);
        printf("\nCurrent B-Tree:\n");
        display_b_tree(b_tree_plus.index);

		printf("\n\nEnter your choice:\n1. Insert data\n2. Remove by value\n3. Search by value\n4. Reset Hash\n5. Exit\n >> ");
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

            case 4: // Caso de reset da hash
                reset_array(&b_tree_plus.content);
                reset_b_tree(&b_tree_plus.index);
                printf("\nB-Tree Plus reseted!");
                break;

            case 5: // Caso de saída do programa
                delete_array(&b_tree_plus.content);
                delete_b_tree(&b_tree_plus.index);
                exit(0);
            
            default:
        }
	}
	return 0;
}