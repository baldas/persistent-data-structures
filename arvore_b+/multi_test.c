// Programa de teste que executa múltiplas instâncias de `arvore_persistente` em
// paralelo, em lotes do tamanho do número de núcleos lógicos detectados.
// Cada filho redireciona sua entrada padrão para `test.txt` e chama
// o executável `./arvore_persistente` via `execvp`.

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

#define TESTES 20

// Cria e executa a tarefa: chama `./arvore_persistente lifetime id`
// - id: identificador da tarefa (usado também para compor o nome do pool)
// - lifetime: argumento passado para controlar o tempo de vida no programa
void task(int id, int lifetime) {

    char lifetime_buffer[64] = "";
    snprintf(lifetime_buffer, sizeof(char) * 64, "%d", lifetime);

    char id_buffer[64] = "";
    snprintf(id_buffer, sizeof(char) * 64, "%d", id);

    // Args para execvp: nome do binário seguido dos argumentos
    char *args[] = {"./arvore_persistente", lifetime_buffer, id_buffer, NULL};
    
    // Substitui o processo atual pelo executável alvo
    if (execvp(args[0], args) == -1) {
        perror("Erro ao rodar o seu executável");
        exit(EXIT_FAILURE);
    }
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

    // Número total de tarefas a serem executadas (padrão definido por TESTES)
    int total_tarefas = TESTES;

    // Se fornecido um argumento, usa-o como total de tarefas
    if (argc == 2) {
        total_tarefas = atoi(argv[1]);
    }

    int tarefas_criadas = 0;
    int processos_ativos = 0;

    // Loop principal: cria filhos em lotes enquanto houver tarefas
    while (tarefas_criadas < total_tarefas || processos_ativos > 0) {
        
        // Cria filhos até encher o lote ou esgotar as tarefas
        while (processos_ativos < num_cores && tarefas_criadas < total_tarefas) {
            pid_t pid = fork();

            if (pid < 0) {
                perror("Erro no fork");
                exit(EXIT_FAILURE);
            }

            if (pid == 0) {

                // Código do filho
                int id_tarefa = tarefas_criadas + 1;
                printf("[Filho] Processando tarefa #%d (PID: %d) em um core...\n", id_tarefa, getpid());

                // Abre o arquivo de entrada de testes e redireciona para stdin
                int arquivo_fd = open("test.txt", O_RDONLY);
                if (arquivo_fd < 0) {
                    perror("Erro ao abrir o arquivo test.txt");
                    exit(EXIT_FAILURE);
                }

                if (dup2(arquivo_fd, STDIN_FILENO) < 0) {
                    perror("Erro no dup2");
                    exit(EXIT_FAILURE);
                }

                close(arquivo_fd);
                
                // Executa a tarefa (substitui o processo filho)
                task(id_tarefa, id_tarefa-1);
                
                // Se o exec falhar, o código abaixo seria executado; mantido por segurança
                printf("[Filho] Tarefa #%d CONCLUÍDA.\n", id_tarefa);
                exit(EXIT_SUCCESS);

            }

            // Pai: atualiza contadores e continua criando filhos até encher o lote
            tarefas_criadas++;
            processos_ativos++;
        }

        // Pai espera por qualquer filho terminar antes de criar mais (controle de lote)
        if (processos_ativos > 0) {
            wait(NULL); 
            processos_ativos--;
        }
    }

    printf("\n[PAI] Todas as %d tarefas foram processadas com sucesso em lotes de %ld!\n", total_tarefas, num_cores);
    return 0;
}