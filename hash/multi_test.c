// Programa de teste paralelo para executar várias instâncias do binário da
// estrutura hash em concorrência.
//
// O objetivo principal é simular um cenário de carga em lote, em que cada
// processo filho recebe um conjunto de entradas de teste e executa o binário
// correspondente (persistente ou volátil) com argumentos específicos de
// lifetime e nome do pool. O programa também coordena a criação de filhos em
// lotes limitados pelo número de núcleos disponíveis, evitando a geração
// excessiva de processos simultâneos.
//
// A lógica de execução é:
// 1. detectar o número de núcleos lógicos;
// 2. criar filhos em batches controlados;
// 3. redirecionar a entrada padrão para arquivos de teste;
// 4. invocar o executável alvo via execvp;
// 5. reaproveitar o processo para recuperação do estado persistente quando
//    necessário.

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

// Estrutura que reúne os dados de uma tarefa de teste.
// Ela encapsula o identificador do processo, o lifetime configurado, o nome do
// arquivo de entrada e o nome do pool persistente a ser usado pelo binário.
typedef struct child_data {
    int id;
    int pid;
    int lifetime;
    char input_name[BUFFER_SIZE];
    char executable_name[BUFFER_SIZE];
    char pool_name[BUFFER_SIZE];
} Child;

// Executa o binário de teste com os parâmetros apropriados.
//
// A função monta a linha de comando do processo e substitui o processo atual por
// uma nova instância do executável usando execvp. Isso permite reutilizar o
// mesmo código do filho sem criar uma camada extra de processo em C.
//
// Argumentos esperados pelo executável:
//   - lifetime
//   - pool_name
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

// Configura a entrada padrão do processo neto para um arquivo de teste.
//
// O processo filho cria um neto para executar o binário real. O neto recebe a
// entrada do teste por redirecionamento do descritor STDIN. Esse padrão permite
// rodar a aplicação sem interação manual e com arquivos de cenário definidos.
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

// Coordena a execução do processo filho e, quando necessário, uma etapa de
// recuperação do estado persistente.
//
// Em primeiro lugar, o filho cria um processo neto para rodar o executável de
// teste com entrada redirecionada. Depois da execução inicial, caso o binário
// termine corretamente, o código prepara um arquivo de entrada para a etapa de
// recuperação e executa novamente o programa, simulando a reabertura da estrutura
// persistente a partir do pool gerado anteriormente.
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

int main(int argc, char* argv[]) {

    // Detecta o número de núcleos lógicos disponíveis para controlar o tamanho
    // do lote de processos simultâneos. Isso evita criar um número excessivo de
    // filhos em máquinas com poucos recursos.
    long num_cores = sysconf(_SC_NPROCESSORS_ONLN);

    if (num_cores < 1) {
        perror("Não foi possível detectar o número de cores. Usando padrão 1.");
        num_cores = 1;
    }

    printf("[PAI] Detectados %ld processadores lógicos disponíveis.\n", num_cores);
    printf("[PAI] O tamanho do lote de processos paralelos será: %ld\n\n", num_cores);

    // Parâmetros de teste usados para montar diferentes combinações de execução.
    //
    // - tasks: número de tarefas por cenário
    // - test_lifetimes: valores de lifetime utilizados nos testes
    // - test_files: arquivos de entrada já estruturados para cada cenário
    // - test_execs: binários que serão executados em paralelo
    int tasks = 20;
    int processos_ativos = 0;
    int test_lifetimes[] = {0, 1, 2, 3};
    char test_files[TEST_SIZE][BUFFER_SIZE] = {"./Testes/teste1","./Testes/teste2","./Testes/teste3","./Testes/teste4"};
    char test_execs[EXEC_SIZE][BUFFER_SIZE] = {"hash_persistente","hash_volatil"};

    // Se for fornecido um argumento na linha de comando, ele substitui o valor
    // padrão do total de tarefas a serem geradas.
    if (argc >= 2) {
        tasks = atoi(argv[1]);
    }

    // Loop principal: cria filhos em lotes enquanto houver tarefas a processar.
    // Para cada executável, para cada arquivo de teste e para cada lifetime, o
    // código dispara `tasks` instâncias do teste em paralelo, respeitando o limite
    // máximo de processos ativos.
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

                    // Mantém o número de processos ativos limitado ao número de
                    // núcleos detectados, aguardando a finalização de algum filho
                    // antes de criar o próximo lote.
                    while (processos_ativos >= num_cores) {
                        wait(NULL); 
                        processos_ativos--;
                    }
                }
            }
        }
    }

    // Espera por todos os filhos restantes antes de encerrar o processo pai.
    while (processos_ativos > 0) {
        wait(NULL); 
        processos_ativos--;
    }

    printf("\n[PAI] Todas as %d tarefas foram processadas com sucesso em lotes de %ld!\n", tasks, num_cores);
    return 0;
}