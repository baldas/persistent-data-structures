#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#define BUFFER_SIZE 64
#define DEFAULT -1

#define DATA_SIZE 8
#define SIZE_RATE 2
#define EXPAND_RATE 0.75
#define REDUCTION_RATE (EXPAND_RATE / SIZE_RATE)

bool init_array(int** data) {

    if ((*data) != NULL) {
        free(*data);
    }

    (*data) = malloc(DATA_SIZE* sizeof(int));

    for (int i = 0; i < DATA_SIZE; i++) {
        (*data)[i] = DEFAULT;
    }

    return true;
}

bool delete_array(int** data) {

    if ((*data) != NULL) {
        free(*data);
    }

    (*data) = NULL;

    return true;
}

bool reset_array(int** data) {
    return delete_array(data) && init_array(data);
}

bool insert_data(int* data, int value) {

    if (data[DATA_SIZE-1] != DEFAULT) {
        return false;
    }

    for (int i = 0; i < DATA_SIZE; i++) {
        if (data[i] == DEFAULT) {
            data[i] = value;
            return true;
        }

        if (data[i] < value) {
            continue;
        }

        if (data[i] > value) {
            int temp = data[i];
            data[i] = value;
            value = temp;
        }

    }

    return false;
}

bool remove_data(int* data, int value) {

    if (data[0] == DEFAULT) {
        return false;
    }

    for (int i = 0; i < DATA_SIZE; i++) {

        if (data[i] < value) {
            continue;
        }

        if (data[i] > value) {
            return false;
        }

        int j = i;
        while (j < DATA_SIZE && data[j] == value) {
            j++;
        }

        while (i < j && j < DATA_SIZE) {
            data[i] = data[j];
            i++;
            j++;
        }

        while (i < DATA_SIZE) {
            data[i] = DEFAULT;
            i++;
        }

    }

    return true;
}

int search_data(int* data, int value) {

    if (data[0] == DEFAULT) {
        return -1;
    }

    for (int i = 0; i < DATA_SIZE; i++) {

        if (data[i] < value) {
            continue;
        }

        if (data[i] > value) {
            return -1;
        }

        return i;
    }

    return -1;
}

bool display_data(int* data) {

    printf("[");

    if (data[0] == DEFAULT) {
        printf(" ");
    }

    for (int i = 0; i < DATA_SIZE && data[i] != DEFAULT; i++) {

        printf("%d", data[i]);

        if (i+1 < DATA_SIZE && data[i+1] != DEFAULT) {
            printf(" ");
        }
    }

    printf("]");

    return true;
}

int main() {

    int option, temp;
    char buffer[BUFFER_SIZE];
    int* data = NULL;
    init_array(&data);

    // Menu de interação
	while (true) {
        printf("\n\nCurrent data: ");
		display_data(data);

		printf("\n\nEnter your choice:\n1. Insert data\n2. Remove by value\n3. Search by value\n4. Reset Hash\n5. Exit\n >> ");
		fgets(buffer, BUFFER_SIZE-1, stdin);
        option = atoi(buffer);

        // Alterna entre as possíveis opções do menu
        switch (option) {

            case 1: // Caso da inserção
                printf("Enter data to be inserted: ");
                fgets(buffer, BUFFER_SIZE-1, stdin);

                if (insert_data(data, atoi(buffer))) {
                    printf("Data inserted!");
                } else {
                    printf("Data not inserted.");
                }
                break;

            case 2: // Caso da remoção de itens
                printf("Enter value to be removed: ");
                fgets(buffer, BUFFER_SIZE-1, stdin);
                if (remove_data(data, atoi(buffer))) {
                    printf("Data removed!");
                } else {
                    printf("Data not removed.");
                }
                break;

            case 3: // Caso da busca (para ver se há um dado item)
                printf("Enter value to be searched: ");
                fgets(buffer, BUFFER_SIZE-1, stdin);
                if ((temp = search_data(data, atoi(buffer))) >= 0) {
                    printf("Data found at position %d.", temp+1);
                } else {
                    printf("No data found.");
                }
                break;

            case 4: // Caso de reset da hash
                reset_array(&data);
                printf("\nArray reseted!");
                break;

            case 5: // Caso de saída do programa
                delete_array(&data);
                exit(0);
            
            default:
        }
	}
	return 0;
}