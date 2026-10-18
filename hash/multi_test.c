// Programa de teste que executa múltiplas instâncias de `arvore_persistente` em
// paralelo, em lotes do tamanho do número de núcleos lógicos detectados.
// Cada filho redireciona sua entrada padrão para `test.txt` e chama
// o executável `./arvore_persistente` via `execvp`.

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

// Cria e executa a tarefa: chama `./arvore_persistente lifetime id`
// - id: identificador da tarefa (usado também para compor o nome do pool)
// - lifetime: argumento passado para controlar o tempo de vida no programa
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
        fprintf(fp,"41\n./Resultados/%s\n",data->pool_name);
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

        remove(name_buffer)

        task(data);
    }

    exit(WIFEXITED(status) ? WEXITSTATUS(status) : EXIT_FAILURE);
}

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
    char test_execs[EXEC_SIZE][BUFFER_SIZE] = {"hash_persistente","hash_volatil"};

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