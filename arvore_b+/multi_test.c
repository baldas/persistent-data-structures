/*
 * Programa de teste responsável por disparar múltiplas execuções da estrutura
 * B+ em paralelo, agrupando children em lotes com tamanho equivalente ao número
 * de núcleos lógicos detectados no sistema.
 *
 * Cada processo filho redireciona sua entrada padrão para um arquivo de teste e
 * executa o binário alvo usando execvp, simulando uma carga concorrente sobre as
 * versões persistente e volátil da árvore. O objetivo principal é validar o
 * comportamento do programa em cenários de execução simultânea e recuperação de
 * estado conforme a energia configurada em cada tarefa.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <string.h>

#define BUFFER_SIZE 64
#define LIFE_SIZE 4
#define EXEC_SIZE 2
#define TEST_SIZE 4

typedef struct child_data {
    int id;
    int pid;
    int lifetime;
    char input_name[BUFFER_SIZE];
    char executable_name[BUFFER_SIZE];
    char pool_name[BUFFER_SIZE];
} Child;

/*
 * Executa a tarefa de um processo filho.
 *
 * A rotina monta o nome do binário e os argumentos que serão passados ao
 * executável por meio de execvp. O parâmetro lifetime controla a energia da
 * estrutura, enquanto o nome do pool identifica o estado persistente da tarefa.
 */
void task(Child * data) {

    char lifetime_buffer[BUFFER_SIZE] = "";
    snprintf(lifetime_buffer, BUFFER_SIZE * sizeof(char), "%d", data->lifetime);

    char name_buffer[BUFFER_SIZE];
    snprintf(name_buffer, BUFFER_SIZE * sizeof(char), "./%s", data->executable_name);

    // Args para execvp: nome do binário seguido dos argumentos
    char *args[] = {name_buffer, lifetime_buffer, data->pool_name, NULL};
    
    // Substitui o processo atual pelo executável alvo
    if (execvp(args[0], args) == -1) {
        perror("Erro ao rodar o seu executável");
        exit(EXIT_FAILURE);
    }
}

/*
 * Código do neto do processo principal.
 *
 * O filho cria um descritor para o arquivo de entrada do cenário de teste,
 * redireciona STDIN para esse arquivo e, em seguida, executa a tarefa correta.
 * Essa etapa simula a execução da estrutura em um ambiente controlado de dados
 * de entrada pré-definidos.
 */
void grandchild_code(Child* data) {
    data->input_name[BUFFER_SIZE-5] = '\0';

    int arquivo_fd = open(strcat(data->input_name, ".txt"), O_RDONLY);

    if (arquivo_fd < 0) {
        perror("Erro ao abrir o arquivo de entrada para os testes.");
        exit(EXIT_FAILURE);
    }

    if (dup2(arquivo_fd, STDIN_FILENO) < 0) {
        perror("Erro no dup2");
        exit(EXIT_FAILURE);
    }

    close(arquivo_fd);
    
    task(data);

    exit(EXIT_FAILURE);
}

/*
 * Código do processo filho.
 *
 * O filho dispara um neto para executar o arquivo de teste e aguarda a sua
 * conclusão. Se a execução finalizar com sucesso, ele gera um arquivo temporário
 * de recuperação, redireciona a entrada padrão para esse mock e reinicia a
 * estrutura para uma segunda execução de recuperação do estado persistente.
 */
void child_code(Child * data) {
    printf("[Filho] Processando tarefa #%d (PID: %d) em um core...\n", data->id, data->pid);

    pid_t pid_neto = fork();
    
    if (pid_neto < 0) {
        perror("Erro no fork do neto");
        exit(EXIT_FAILURE);
    }

    if (pid_neto == 0) {
        grandchild_code(data); 
        exit(EXIT_FAILURE);
    }

    int status;
    waitpid(pid_neto, &status, 0);

    if (WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS) {

        char name_buffer[BUFFER_SIZE];
        snprintf(name_buffer, BUFFER_SIZE * sizeof(char), "recover%s.txt", data->pool_name);

        FILE* fp = fopen(name_buffer, "w");
        if (fp == NULL) {
            perror("Erro ao escrever o arquivo de entrada para a recuperação.");
            exit(EXIT_FAILURE);
        }
        fprintf(fp,"41\n./Resultados/%s\n5\n",data->pool_name);
        fclose(fp);

        int arquivo_fd = open(name_buffer, O_RDONLY);

        if (arquivo_fd < 0) {
            perror("Erro ao abrir o arquivo de entrada para a recuperação.");
            exit(EXIT_FAILURE);
        }

        if (dup2(arquivo_fd, STDIN_FILENO) < 0) {
            perror("Erro no dup2");
            exit(EXIT_FAILURE);
        }

        close(arquivo_fd);

        remove(name_buffer);

        task(data);
    }

    exit(WIFEXITED(status) ? WEXITSTATUS(status) : EXIT_FAILURE);
}

/*
 * Função principal.
 *
 * O processo pai detecta o número de núcleos lógicos disponíveis, define o lote
 * máximo de execuções concorrentes e inicia um conjunto de tarefas em sequência,
 * variando entre executáveis, arquivos de teste e valores de lifetime. O objetivo
 * é gerar paralelismo controlado e validar a robustez da estrutura em cenários
 * de múltiplos processos simultâneos.
 */
int main(int argc, char* argv[]) {

    // Detecta número de núcleos lógicos disponíveis para determinar
    // quantos filhos executar simultaneamente (tamanho do lote)
    long num_cores = sysconf(_SC_NPROCESSORS_ONLN);

    if (num_cores < 1) {
        perror("Não foi possível detectar o número de cores. Usando padrão 1.");
        num_cores = 1;
    }

    printf("[PAI] Detectados %ld processadores lógicos disponíveis.\n", num_cores);
    printf("[PAI] O tamanho do lote de processos paralelos será: %ld\n\n", num_cores);

    // Parâmetros de teste
    int tasks = 20;
    int processos_ativos = 0;
    int test_lifetimes[] = {0, 1, 2, 3};
    char test_files[TEST_SIZE][BUFFER_SIZE] = {"./Testes/teste1","./Testes/teste2","./Testes/teste3","./Testes/teste4"};
    char test_execs[EXEC_SIZE][BUFFER_SIZE] = {"arvore_persistente","arvore_volatil"};

    // Se fornecido um argumento, usa-o como total de tarefas
    if (argc >= 2) {
        tasks = atoi(argv[1]);
    }

    // Loop principal: cria filhos em lotes enquanto houver tarefas
    for (int exec = 0; exec < EXEC_SIZE; exec++) {
        for (int test = 0; test < TEST_SIZE; test++) {
            for (int lifetime = 0; lifetime < LIFE_SIZE; lifetime++) {
                for (int task = 1; task <= tasks; task++) {

                    pid_t pid = fork();
                    if (pid < 0) {
                        perror("Erro no fork");
                        exit(EXIT_FAILURE);
                    }
                    if (pid == 0) {
                        char pool_name [BUFFER_SIZE];
                        snprintf(pool_name, BUFFER_SIZE * sizeof(char), "task%d-%d-%d-%d", exec, test, lifetime, task);

                        Child data = {};
                        data.id = task;
                        data.pid = getpid();
                        data.lifetime = test_lifetimes[lifetime];
                        strcpy(data.input_name,test_files[test]);
                        strcpy(data.executable_name,test_execs[exec]);
                        strcpy(data.pool_name,pool_name);

                        child_code(&data);
                        exit(EXIT_FAILURE);
                    }
                    processos_ativos++;

                    while (processos_ativos >= num_cores) {
                        wait(NULL); 
                        processos_ativos--;
                    }
                }
            }
        }
    }

    while (processos_ativos > 0) {
        wait(NULL); 
        processos_ativos--;
    }

    printf("\n[PAI] Todas as %d tarefas foram processadas com sucesso em lotes de %ld!\n", tasks, num_cores);
    return 0;
}