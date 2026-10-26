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
 *
 * Esse arquivo funciona como uma versão volátil da tabela hash persistente do projeto.
 * Ele mantém a mesma lógica estrutural, mas utiliza `malloc`/`calloc` em memória RAM, sem
 * depender do PMDK. Isso facilita testes locais, experimentos e comparação entre os dois
 * comportamentos: persistente e em memória principal.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <limits.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#define FULL 1.0
#define EMPTY 0.0
#define DEFAULT INT_MIN

#define BUFFER_SIZE 64
#define INITIAL_SIZE 8
#define SIZE_RATE 2
#define EXPAND_RATE 0.75
#define REDUCTION_RATE (EXPAND_RATE / SIZE_RATE)

// Inicialização de variáveis globais
int lifetime = DEFAULT;

/*
 * Estrutura de dados base.
 * Pode ser adaptada para armazenar outros campos ou ponteiros, desde que as funções de
 * cópia, comparação e limpeza também sejam ajustadas. Neste caso, o registro contém apenas
 * um inteiro `num`, o que mantém o exemplo simples e didático.
 *
 * A ideia é manter o menor tipo possível para demonstrar a lógica da hash, sem ocultar o
 * mecanismo principal: cálculo do hash, sondagem linear, marcação de ocupação e expansão.
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
 *
 * A estratégia escolhida é a de tabela hash aberta com sonda linear: quando ocorre colisão,
 * o algoritmo tenta a próxima posição disponível até encontrar uma vaga ou uma chave igual.
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
 *
 * Essa escolha facilita a interação com o menu principal, onde as operações de inserir,
 * buscar, remover e exportar precisam acessar o mesmo objeto em vários pontos do código.
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
 *
 * Neste exemplo, a função é propositalmente simples para manter o código legível e didático.
 * Em aplicações reais, poderia ser substituída por uma função mais robusta ou por uma chave
 * combinada em múltiplos campos.
 */
int hash_function(DATA* data) {
    return (data->num * data->num) % hash->max_size;
}

/*
 * Retorna a taxa de ocupação da tabela (load factor).
 * Esse valor é comparado com os limites EXPAND_RATE e REDUCTION_RATE para decidir se a hash
 * precisa crescer ou diminuir.
 *
 * A carga influencia diretamente a eficiência: quanto maior a taxa, mais colisões tendem a
 * acontecer; por isso o código faz o redimensionamento automático para manter a estrutura
 * em um intervalo aceitável.
 */
double hash_rate() {
    return (((double) hash->size) / ((double) hash->max_size)) * FULL;
}

/*
 * Inicializa a tabela hash.
 * Cria o objeto HASH, define a capacidade inicial e aloca os arrays de dados e flags de
 * ocupação. Todos os slots começam vazios e, assim, prontos para receber registros.
 *
 * A função é chamada no início da execução e também após um reset da estrutura.
 */
void start_hash() {

    if (hash == NULL) {
        hash = (HASH*) malloc(sizeof(HASH));
        hash->size = 0;
        hash->max_size = INITIAL_SIZE;
        if (lifetime != DEFAULT) {
            if (lifetime == 0)
                exit(0);
            lifetime--;
        }
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

            // Controle de tempo de vida (opcional)
            if (lifetime != DEFAULT) {
                if (lifetime == 0)
                    exit(0);
                lifetime--;
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
            if (lifetime != DEFAULT) {
                if (lifetime == 0)
                    exit(0);
                lifetime--;
            }
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

    bool removed = false;

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
                    if (!removed && lifetime != DEFAULT) {
                        if (lifetime == 0)
                            exit(0);
                        lifetime--;
                    }
                    removed = true;
                }
                
                position = position + 1;
                position = position % hash->max_size;
            }

            // Realiza a verificação de se é necessário reduzir o hash para ficar na faixa desejada
            while ((hash_rate() < REDUCTION_RATE) && (hash->max_size > INITIAL_SIZE))
                reduce_hash();

        } else {
            fprintf(stderr, "\n<--Hash vazio-->\n");
        }
    }

    return removed;
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
        if (lifetime != DEFAULT) {
            if (lifetime == 0)
                exit(0);
            lifetime--;
        }
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
void display(FILE* output_file) {

    // Percorre as linhas de 8 itens do hash
    for (int i = 0; i < (hash->max_size / INITIAL_SIZE); i++) {

        // Caso seja a primeira linha, imprime o cabeçalho do hash
        if (i != 0)
            fprintf(output_file, "\n       ");
        else
            fprintf(output_file, "\n\n HASH =");

        // Percorre os itens do hash daquela linha
        for (int j = 0; j < INITIAL_SIZE; j++) {

            // Se tiver um conteúdo naquele espaço, verifica se está ocupado
            if (hash->data[i*INITIAL_SIZE+j] != NULL) {

                // Caso não esteja ocupado, usa a flag de remoção na resposta
                if (hash->occupied[i*INITIAL_SIZE+j])
                    fprintf(output_file, " [%d] ", hash->data[i*INITIAL_SIZE+j]->num);
                else
                    fprintf(output_file, " [!%d] ", hash->data[i*INITIAL_SIZE+j]->num);

            } else
                fprintf(output_file, " [*] ");
        }

        fprintf(output_file, " (%d)", (i+1)*INITIAL_SIZE);
    }
}

// Exporta o conteúdo do array em formato JSON.
// Cada posição do vetor é convertida em uma string representando o valor ativo,
// o tombstone (marcado como removido) ou o slot vazio.
bool json_content(FILE* output_file) {

    for (int i = 0; i < hash->max_size; i++) {

        if (hash->data[i] == NULL) {
            fprintf(output_file, "\"**\"");
        } else {
            if (hash->occupied[i])
                fprintf(output_file,"\"%d\"", hash->data[i]->num);
            else
                fprintf(output_file,"\"!%d\"", hash->data[i]->num);
        }
        if (!(i == hash->max_size -1))
            fprintf(output_file,", ");
    }

    return true;
}

// Realiza a exportação dos dados para um arquivo JSON.
// Além do conteúdo da tabela, o arquivo registra o horário atual e a carga de
// energia (lifetime), mantendo um histórico de exportações em disco.
bool export_json(char * buffer) {
    buffer[BUFFER_SIZE-6] = '\0';
    FILE* output_file = fopen(strcat(buffer,".json"),"a+");
    time_t currentTime = time(NULL);

    char buffer_time[BUFFER_SIZE];
    struct tm *info_tempo = localtime(&currentTime);
    strftime(buffer_time, sizeof(buffer), "%A, %d/%m/%Y - %H:%M:%S", info_tempo);

    fprintf(output_file, "{\n\"time\":\"%s\",\n", buffer_time);

    fprintf(output_file, "\"charge\": \"%d\",\n",lifetime);

    fprintf(output_file, "\"content\":[\n");
    json_content(output_file);
    fprintf(output_file, "]\n}\n");

    fclose(output_file);
    return true;
}

// Realiza a exportação dos dados para um arquivo texto.
// O arquivo inclui ticket, horário da exportação, estado atual da energia e
// representação visual da tabela em blocos de 8 posições.
bool export_text(char * buffer, int ticket) {
    buffer[BUFFER_SIZE-5] = '\0';
    FILE* output_file = fopen(strcat(buffer,".txt"),"a+");
    time_t currentTime = time(NULL);

    fprintf(output_file, "Data Ticket %d - %s", ticket, ctime(&currentTime));

    if (lifetime >=0) {
        fprintf(output_file, "\nCurrent charge: %dx █\n",lifetime);
    }

    fprintf(output_file, "\nCurrent data:\n");
    display(output_file);

    fprintf(output_file, "\n\nExported by Hash program.\n");
    fprintf(output_file, "----------------------------------------------\n");
    fclose(output_file);
    return true;
}

// Programa principal.
//
// O menu interativo permite inserir, remover, buscar e exportar dados da tabela
// hash. Também oferece funções avançadas como reset da estrutura, energia
// (lifetime), remoção por posição e restauração de itens marcados como removidos.
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
        case 2: case 3: lifetime = atoi(argv[1]); break;
        default:
            perror("Try to use less arguments.\n");
            return 1;
    }

    int option, temp, ticket = 0;
    bool json_export_type = false;
    char buffer[BUFFER_SIZE];
    DATA data;
    start_hash();

    // Menu de interação
	while (true) {
		display(stdout);

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
                data.num = atoi(buffer);
                if (insert_data(&data)) {
                    printf("Data inserted!");
                } else {
                    printf("Data not inserted.");
                }
                break;

            case 2: // Caso da remoção de ites
                printf("Enter value to be removed: ");
                fgets(buffer, BUFFER_SIZE-1, stdin);
                data.num = atoi(buffer);
                if (remove_data(&data)) {
                    printf("Data removed!");
                } else {
                    printf("Data not removed.");
                }
                break;

            case 3: // Caso da busca (para ver se há um dado item)
                printf("Enter value to be searched: ");
                fgets(buffer, BUFFER_SIZE-1, stdin);
                data.num = atoi(buffer);
                if ((temp = search_data(&data)) >= 0) {
                    printf("Data found at position %d.", temp+1);
                } else {
                    printf("No data found.");
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
                                export_json(buffer);
                            else
                                export_text(buffer, ++ticket);

                            printf("Hash exported!\n");

                        } else
                            printf("Failed to export Hash!\n");
                        break;

                    case 2: // Caso de reset da estrutura
                        end_hash();
                        if (lifetime != DEFAULT) {
                            if (lifetime == 0)
                                exit(0);
                            lifetime--;
                        }
                        
                        start_hash();
                        printf("\nHash reseted!");
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
                        temp = atoi(buffer);
                        if (remove_position(temp-1)) {
                            printf("Position removed!");
                        } else {
                            printf("Position not removed.");
                        }
                        break;

                    case 6: // Caso de Restauração por Posição
                        printf("Enter position to be restored: ");
                        fgets(buffer, BUFFER_SIZE-1, stdin);
                        temp = atoi(buffer);
                        if (restore_position(temp-1)) {
                            printf("Position restored!");
                        } else {
                            printf("Position not restored.");
                        }
                        break;

                    default:
                }break;

            case 5:// Caso de saída do programa
                end_hash();
                exit(0);
                break; 

            case 41: //Opção secreta para exportar em json com 1 comando
                fgets(buffer, BUFFER_SIZE-1, stdin);
                buffer[strcspn(buffer, "\n")] = '\0';
                export_json(buffer);
                break;
            
            default:
        }
	}
	return 0;
}