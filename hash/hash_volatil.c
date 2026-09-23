/*
 * Esta implementação usa uma tabela hash aberta com endereçamento linear em memória RAM.
 * O objetivo é armazenar registros do tipo DATA em um vetor de ponteiros e manter uma
 * estrutura auxiliar de ocupação para distinguir posições vazias de posições ocupadas por
 * itens removidos logicamente.
 *
 * A tabela cresce e diminui dinamicamente conforme a taxa de ocupação do hash muda. Quando
 * a carga se aproxima de 75%, ocorre expansão; quando a carga cai muito abaixo do limite,
 * ocorre redução. A lógica foi pensada para manter a estrutura balanceada sem perder a
 * simplicidade de implementação em C.
 *
 * A hash organiza os dados em posições calculadas por uma função de espalhamento, e, em
 * caso de colisão, a busca continua linearmente até encontrar um slot livre ou o elemento
 * correspondente. O array `occupied` funciona como um marcador de estado de cada posição.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#define FULL 1.0
#define EMPTY 0.0

#define BUFFER_SIZE 64
#define INITIAL_SIZE 8
#define SIZE_RATE 2
#define EXPAND_RATE 0.75
#define REDUCTION_RATE (EXPAND_RATE / SIZE_RATE)

/*
 * Estrutura de dados base.
 * Pode ser adaptada para armazenar outros campos ou ponteiros, desde que as funções de
 * cópia, comparação e limpeza também sejam ajustadas. Neste caso, o registro contém apenas
 * um inteiro `num`, o que mantém o exemplo simples e didático.
 */
typedef struct {
    int num;
} DATA;

/*******Funções nesta região:*******/
void create_pointers(DATA*);
void copy_pointers_data(DATA*, DATA*);
void clean_pointers(DATA*);
void copy_data(DATA*, DATA*);
int compare_data(DATA*, DATA*);
DATA* add_data(DATA*, DATA*);
/**************************************/

void create_pointers(DATA* data) {
    // Caso tenha ponteiros no DATA, iniciá-los aqui com "calloc(amount, type_size)"
}

void copy_pointers_data(DATA* dest, DATA* origin) {
    // Caso tenha ponteiros no DATA, copia seus dados nos ponteiros do destino"
}

void clean_pointers(DATA* data) {
    // Caso tenha ponteiros no DATA, limpá-los aqui com "free()";
}

// Pode mudar a depender da struct DATA
void copy_data(DATA* dest, DATA* origin) {

    dest->num = origin->num;

    copy_pointers_data(dest, origin);
}

// Pode mudar a depender da struct DATA
int compare_data(DATA* dest, DATA* origin) {
    return (dest->num - origin->num);
}

// Aloca espaço para 1 dado
DATA* add_data(DATA* dest, DATA* origin) {
    if (dest == NULL) {
        dest = (DATA*) calloc(1, sizeof(DATA));
        create_pointers(dest);
        copy_data(dest, origin);
    }
    return dest;
}

/*
 * Estrutura principal da tabela hash.
 * - size: quantidade de registros atualmente ativos;
 * - max_size: capacidade total do vetor;
 * - data: array de ponteiros para os registros armazenados;
 * - occupied: flags que indicam se cada posição está ocupada, vazia ou marcada como remoção.
 *
 * A tabela funciona como um conjunto de slots, onde cada slot pode apontar para um registro
 * ou permanecer NULL. O vetor `occupied` separa o conceito de slot "vazio" do slot "ocupado
 * por um registro removido logicamente".
 */
typedef struct {

    int size;
    int max_size;

    DATA** data;
    bool* occupied;

} HASH;

/*
 * Ponteiro global para a tabela hash ativa.
 * Como a implementação é feita em um único programa interativo, manter a estrutura em uma
 * variável global simplifica o acesso e a manipulação em todas as funções.
 */
HASH* hash = NULL;

/*******Funções nesta região:*******/
int hash_function(DATA*);
double hash_rate();
void start_hash();
void expand_hash();
void reduce_hash();
bool insert_data(DATA*);
bool remove_position(int);
bool remove_data(DATA*);
bool delete_data(int);
int search_data(DATA*);
void end_hash();
void display();
/**************************************/

/*
 * Função de espalhamento.
 * A chave é o valor numérico do registro e o cálculo simples (dado * dado) % max_size
 * produz um índice inicial para a tabela. Em caso de colisão, a implementação continua
 * linearmente até encontrar uma posição adequada.
 */
int hash_function(DATA* data) {
    return (data->num * data->num) % hash->max_size;
}

/*
 * Retorna a taxa de ocupação da tabela (load factor).
 * Esse valor é comparado com os limites EXPAND_RATE e REDUCTION_RATE para decidir se a hash
 * precisa crescer ou diminuir.
 */
double hash_rate() {
    return (((double) hash->size) / ((double) hash->max_size)) * FULL;
}

/*
 * Inicializa a tabela hash.
 * Cria o objeto HASH, define a capacidade inicial e aloca os arrays de dados e flags de
 * ocupação. Todos os slots começam vazios e, assim, prontos para receber registros.
 */
void start_hash() {

    if (hash == NULL) {
        hash = (HASH*) malloc(sizeof(HASH));
        hash->size = 0;
        hash->max_size = INITIAL_SIZE;
        hash->data = calloc(INITIAL_SIZE, sizeof(DATA*));
        hash->occupied = calloc(INITIAL_SIZE, sizeof(bool));
    }
}

/*
 * Expande a hash quando a carga chega ao limite esperado.
 * O processo cria um novo vetor de maior capacidade, re-insere todos os elementos ativos na
 * nova posição e renova o estado de ocupação. Esse passo reduz o número de colisões e evita
 * que a tabela se torne excessivamente densa.
 */
void expand_hash() {

    if ((hash != NULL) && (hash_rate() >= EXPAND_RATE)) {

        int old_size = hash->max_size;
        int new_size = hash->max_size * SIZE_RATE;

        // Alocando o espaço para o tamanho expandido
        DATA** new_data = calloc(new_size, sizeof(DATA*));
        bool * new_occupied = calloc(new_size, sizeof(bool));

        // Checando se as alocações de memória deram certo
        if (new_data != NULL && new_occupied != NULL) {

            hash->max_size = new_size;
            for (int i = 0; i < old_size; i++) {

                // Passando ponteiro do registro para o novo hash
                if (hash->occupied[i]) {
                    int new_position = hash_function(hash->data[i]);
    
                    while(new_occupied[new_position]) {
                        new_position = new_position + 1;
                        new_position = new_position % new_size;
                    }
                    new_data[new_position] = hash->data[i];
                    new_occupied[new_position] = true;
                }
    
                // Limpando dados não usados
                if (!hash->occupied[i] && hash->data[i] != NULL) {
                    hash->max_size = old_size;
                    delete_data(i);
                    hash->max_size = new_size;
                }
            }
    
            // Limpando as antigas alocações e inserindo as novas na hash atual
            free(hash->data);
            free(hash->occupied);
    
            hash->data = new_data;
            hash->occupied = new_occupied;
        }
    }
}

/*
 * Reduz a tabela quando ela está pouco ocupada.
 * A estrutura volta para um tamanho menor, preservando os dados existentes e reaproveitando
 * a mesma estratégia de rehash. Essa operação reduz consumo de memória e mantém a taxa de
 * ocupação em um intervalo razoável.
 */
void reduce_hash() {

    if ((hash != NULL) && (hash_rate() < REDUCTION_RATE) && (hash->max_size > INITIAL_SIZE)) {

        int old_size = hash->max_size;
        int new_size = hash->max_size / SIZE_RATE;

        // Alocando o espaço para o tamanho expandido
        DATA** new_data = calloc(new_size, sizeof(DATA*));
        bool * new_occupied = calloc(new_size, sizeof(bool));

        // Checando se as alocações de memória deram certo
        if (new_data != NULL && new_occupied != NULL) {

            hash->max_size = new_size;
            for (int i = 0; i < old_size; i++) {

                // Passando ponteiro do registro para o novo hash
                if (hash->occupied[i]) {
                    int new_position = hash_function(hash->data[i]);
    
                    while(new_occupied[new_position]) {
                        new_position = new_position + 1;
                        new_position = new_position % new_size;
                    }
                    new_data[new_position] = hash->data[i];
                    new_occupied[new_position] = true;
                }
    
                // Limpando dados não usados
                if (!hash->occupied[i] && hash->data[i] != NULL) {
                    hash->max_size = old_size;
                    delete_data(i);
                    hash->max_size = new_size;
                }
            }
    
            // Limpando as antigas alocações e inserindo as novas na hash atual
            free(hash->data);
            free(hash->occupied);
    
            hash->data = new_data;
            hash->occupied = new_occupied;
        }
    }
}

/*
 * Insere um registro na tabela hash.
 * Primeiro, a função tenta encontrar um slot livre usando sondagem linear. Quando encontra a
 * posição correta, o dado é salvo e o contador `size` é atualizado. Se houver necessidade,
 * a estrutura também dispara a expansão automática.
 */
bool insert_data(DATA* data) {

    if (hash != NULL) {

        // Se tiver espaço, tenta inserir
        if (hash_rate() < FULL) {

            int position = hash_function(data);

            // Enquanto tiver locais ocupados, vai procurando o primeiro local vazio
            while (hash->occupied[position]) {
                position = position + 1;
                position = position % hash->max_size;
            }

            // Se nao tiver dados anteriores, aloca espaço e copia os dados. Caso tenha, apenas faz a cópia
            if (hash->data[position] == NULL) {
                hash->data[position] = add_data(hash->data[position], data);
            } else {
                copy_data(hash->data[position], data);
            }

            // Atualiza a flag de ocupação e a contagem de itens no hash
            hash->occupied[position] = true;
            hash->size = hash->size + 1;
            expand_hash();

            return true;

        } else {
            fprintf(stderr, "\n<--Hash cheio-->\n");
        }
    }
    return false;
}

/*
 * Remove logicamente um item em uma posição específica.
 * Em vez de limpar o dado fisicamente, a flag `occupied` é desativada. A implementação mantém
 * o valor no vetor para fins de compatibilidade com a sondagem linear, enquanto marca o slot
 * como removido para a estrutura não considerar aquele item em buscas futuras.
 */
bool remove_position(int position) {

    if (hash != NULL && (position >=0 && position < hash->max_size)) {

        // Caso esteja ocupado, remove logicamente
        if (hash->occupied[position]) {
            hash->occupied[position] = false;
            hash->size = hash->size - 1;
            return true;
        }
    }
    return false;
}

/*
 * Remove todas as ocorrências de um valor específico.
 * A varredura percorre a sequência linear da tabela, identifica os registros iguais ao valor
 * informado e desativa a flag de ocupação. A redução da estrutura também pode ser executada
 * após a remoção para manter a carga em faixa desejada.
 */
bool remove_data(DATA* data) {

    if (hash != NULL) {

        // Se tiver itens, tenta remover
        if (hash_rate() > EMPTY) {

            int position = hash_function(data);

            // Enquanto tiver locais ocupados, vai procurando os locais com o item para deletar
            while (hash->data[position] != NULL) {

                // Se for igual ao item a ser removido, e não ter sido removido anteriormente, é removido
                if ((compare_data(hash->data[position], data) == 0) && (hash->occupied[position])) {
                    hash->occupied[position] = false;
                    hash->size = hash->size - 1;
                }
                
                position = position + 1;
                position = position % hash->max_size;
            }

            // Realiza a verificação de se é necessário reduzir o hash para ficar na faixa desejada
            while ((hash_rate() < REDUCTION_RATE) && (hash->max_size > INITIAL_SIZE))
                reduce_hash();

            return true;

        } else {
            fprintf(stderr, "\n<--Hash vazio-->\n");
        }
    }

    return false;
}

/*
 * Remove fisicamente um dado em uma posição válida.
 * A função libera a memória do registro e zera o ponteiro do slot. Se o item estava ativo,
 * também decrementa o contador `size` e tenta reduzir a tabela para manter a ocupação sob
 * controle.
 */
bool delete_data(int position) {

    // Se o endereço for válido, é deletado
    if ((hash != NULL) && ((position >= 0) && (position < hash->max_size)) && (hash->data[position] != NULL)) {
        clean_pointers(hash->data[position]);
        free(hash->data[position]);
        hash->data[position] = NULL;

        // Caso tivesse ativo, removia
        if (hash->occupied[position]) {
            hash->occupied[position] = false;
            hash->size = hash->size - 1;
            reduce_hash();
        }
        return true;
    }
    return false;
}

/*
 * Restaura um slot removido logicamente.
 * Essa operação reativa uma posição que ainda guarda dados válidos, mas foi marcada como
 * fora de uso. Ela é útil em cenários em que a remoção é apenas lógica e o item pode ser
 * recuperado sem reconstrução da estrutura.
 */
bool restore_position(int position) {
    if ((hash != NULL) && ((position >= 0) && (position < hash->max_size)) && (hash->data[position] != NULL) && (!hash->occupied[position])) {
        hash->occupied[position] = true;
        hash->size = hash->size + 1;
        return true;
    }
    return false;
}

/*
 * Busca um registro na tabela.
 * O algoritmo calcula a posição inicial pela função hash, percorre a sequência linear até
 * encontrar o valor desejado ou um slot vazio, e retorna o índice da primeira ocorrência
 * válida. Caso o item não exista, retorna -1.
 */
int search_data(DATA* data) {

    if (hash != NULL) {

        // Se tiver itens, tenta remover
        if (hash_rate() > EMPTY) {

            int position = hash_function(data);

            // Enquanto tiver locais ocupados, vai procurando os locais com o item a ser buscado
            while (hash->data[position] != NULL) {

                // Se for igual ao item a ser removido, e não ter sido removido anteriormente, é removido
                if ((compare_data(hash->data[position], data) == 0) && (hash->occupied[position])) {
                    return position;
                }
                
                position = position + 1;
                position = position % hash->max_size;
            }

        } else {
            fprintf(stderr, "\n<--Hash vazio-->\n");
        }
    }
    return -1;
}

/*
 * Finaliza a hash e desaloca toda a memória alocada.
 * O processo percorre todos os slots, libera registros existentes e então libera os vetores
 * internos e a própria estrutura HASH. Isso evita vazamentos e deixa o estado da aplicação
 * consistente ao encerrar a execução.
 */
void end_hash() {
    if (hash != NULL) {
        
        // Deleta os dados do hash
        for (int i = 0; i < hash->max_size; i++) {
            delete_data(i);
        }

        // Remove os ponteiros e invalida a hash
        free(hash->data);
        free(hash->occupied);
        free(hash);
        hash = NULL;
    }
}

/*
 * Exibe a tabela hash em linhas agrupadas por blocos de 8 posições.
 * Cada item é mostrado com a forma [valor] quando ativo ou [!valor] quando marcado como
 * remoção lógica. O símbolo [*] indica slot vazio. Essa visualização facilita o diagnóstico
 * do comportamento de colisões e do estado atual da tabela.
 */
void display() {

    // Percorre as linhas de 8 itens do hash
    for (int i = 0; i < (hash->max_size / INITIAL_SIZE); i++) {

        // Caso seja a primeira linha, imprime o cabeçalho do hash
        if (i != 0)
            printf("\n       ");
        else
            printf("\n\n HASH =");

        // Percorre os itens do hash daquela linha
        for (int j = 0; j < INITIAL_SIZE; j++) {

            // Se tiver um conteúdo naquele espaço, verifica se está ocupado
            if (hash->data[i*INITIAL_SIZE+j] != NULL) {

                // Caso não esteja ocupado, usa a flag de remoção na resposta
                if (hash->occupied[i*INITIAL_SIZE+j])
                    printf(" [%d] ", hash->data[i*INITIAL_SIZE+j]->num);
                else
                    printf(" [!%d] ", hash->data[i*INITIAL_SIZE+j]->num);

            } else
                printf(" [*] ");
        }

        printf(" (%d)", (i+1)*INITIAL_SIZE);
    }
}

int main() {

    int option, temp;
    char buffer[BUFFER_SIZE];
    DATA data;
    start_hash();

    // Menu de interação
	while (true) {
		display();

		printf("\n\nEnter your choice:\n1. Insert data\n2. Remove by position\n3. Remove by value\n4. Search by value\n5. Restore by position\n6. Reset Hash\n7. Exit\n >> ");
		fgets(buffer, BUFFER_SIZE-1, stdin);
        option = atoi(buffer);

        // Alterna entre as possíveis opções do menu
        switch (option) {

            case 1: // Caso da inserção
                printf("Enter data to be inserted: ");
                fgets(buffer, BUFFER_SIZE-1, stdin);
                data.num = atoi(buffer);
                if (insert_data(&data)) {
                    printf("Data inserted!");
                } else {
                    printf("Data not inserted.");
                }
                break;

            case 2: // Caso da remoção posicional
                printf("Enter position to be removed: ");
                fgets(buffer, BUFFER_SIZE-1, stdin);
                temp = atoi(buffer);
                if (remove_position(temp-1)) {
                    printf("Position removed!");
                } else {
                    printf("Position not removed.");
                }
                break;

            case 3: // Caso da remoção de ites
                printf("Enter value to be removed: ");
                fgets(buffer, BUFFER_SIZE-1, stdin);
                data.num = atoi(buffer);
                if (remove_data(&data)) {
                    printf("Data removed!");
                } else {
                    printf("Data not removed.");
                }
                break;

            case 4: // Caso da busca (para ver se há um dado item)
                printf("Enter value to be searched: ");
                fgets(buffer, BUFFER_SIZE-1, stdin);
                data.num = atoi(buffer);
                if ((temp = search_data(&data)) >= 0) {
                    printf("Data found at position %d.", temp+1);
                } else {
                    printf("No data found.");
                }
                break;

            case 5: // Caso de restauração de itens recém-deletados
                printf("Enter position to be restored: ");
                fgets(buffer, BUFFER_SIZE-1, stdin);
                temp = atoi(buffer);
                if (restore_position(temp-1)) {
                    printf("Position restored!");
                } else {
                    printf("Position not restored.");
                }
                break;

            case 6: // Caso de reset da hash
                end_hash();
                start_hash();
                printf("\nHash reseted!");
                break;

            case 7: // Caso de saída do programa
                end_hash();
                exit(0);
            
            default:
        }
	}
	return 0;
}