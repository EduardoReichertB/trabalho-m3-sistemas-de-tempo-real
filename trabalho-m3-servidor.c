#include <pthread.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

// gcc -O2 -Wall trabalho-m3-servidor.c -o trabalho-m3-servidor -pthread
// sudo ./trabalho-m3-servidor

/////////////////////// Fila de requisições aperiódicas ///////////////////////////
typedef void (*job_func_t)(void *arg);
typedef struct job {
  job_func_t func;
  void *arg;
  struct job *next;
} job_t;

//PEGAMOS DA M2, É A TASK_SORT, REPRESENTA A DETECÇÃO DE OBJETOS
//equivalente ao evento de touch
typedef struct {
  int id;
  struct timespec instante_evento;
} sort_evt_t;

// Representa uma tarefa periodica
typedef struct {
  char nome[10];
  long periodo_ms;
  long execucao_us;
} periodic_params_t;

static job_t *queue_head = NULL;
static job_t *queue_tail = NULL;
static pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;

/////////////////////// Enfileira um job (chamado pelas tarefas aperiódicas) ///////////////////////
void enqueue_job(job_func_t f, void *arg) {
  job_t *j = malloc(sizeof(job_t));
  j->func = f;
  j->arg = arg;
  j->next = NULL;

  pthread_mutex_lock(&queue_mutex);

  if (queue_tail)
    queue_tail->next = j;
  else

  queue_head = j;
  queue_tail = j;

  pthread_cond_signal(&queue_cond);
  pthread_mutex_unlock(&queue_mutex);
}

/////////////////////// Retira um job da fila (usado pelo servidor) ///////////////////////
job_t *dequeue_job(void) {
  job_t *j = queue_head;
  if (!j) return NULL;
  queue_head = j->next;
  if (!queue_head) queue_tail = NULL;
  return j;
}

/////////////////////// soma nanossegundos a um timespec (periodo) ///////////////////////
static void timespec_add_ns(struct timespec *t, long ns) {
  t->tv_nsec += ns;
  while (t->tv_nsec >= 1000000000L) {
    t->tv_nsec -= 1000000000L;
    t->tv_sec += 1;
  }
}

typedef struct {
 long period_ns; // Ts (por ex. 10ms = 10*10^6 ns)
 long budget_ns; // Cs (pode ser <= Ts*Umax)
} server_params_t;

void *server_thread(void *arg) {
  server_params_t *params = (server_params_t *)arg;

  long Ts = params->period_ns;
  long Cs = params->budget_ns;

  struct timespec next_release;
  clock_gettime(CLOCK_MONOTONIC, &next_release);

  int periodo = 0;

  struct timespec ultimo_inicio;
  clock_gettime(CLOCK_MONOTONIC, &ultimo_inicio);

  while (1) {

    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_release, NULL);

    /////////////////////// define momento da próxima ativação
    timespec_add_ns(&next_release, Ts);

    periodo++;

    /////////////////////// início do período
    struct timespec start_period;
    clock_gettime(CLOCK_MONOTONIC, &start_period);

    long consumed_ns = 0;
    int jobs_executados = 0;

    printf("\n=== PERIODO %d ===\n", periodo);
    while (consumed_ns < Cs) {
      /////////////////////// pega um job, se existir
      pthread_mutex_lock(&queue_mutex);

      while (!queue_head) {
      /////////////////////// se não há jobs, pode sair do loop de serviço
      pthread_mutex_unlock(&queue_mutex);
      goto end_of_service;
      }

      job_t *j = dequeue_job();
      pthread_mutex_unlock(&queue_mutex);

      /////////////////////// mede tempo antes/depois de executar o job
      struct timespec t_before, t_after;

      clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t_before);

      j->func(j->arg); // executa requisição aperiódica

      clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t_after);

      /////////////////////// atualiza orçamento consumido
      long dt = (t_after.tv_sec - t_before.tv_sec)*1000000000L +
      (t_after.tv_nsec - t_before.tv_nsec);
      consumed_ns += dt;
      jobs_executados++;

      printf("Tempo do job: %.3f ms\n", dt / 1000000.0);

      free(j);

      if (consumed_ns >= Cs) {
        printf("ORCAMENTO ESGOTADO!\n");
        break;
      }
    }

    // Relatório do período
    printf("\n--- RESUMO DO PERIODO %d ---\n", periodo);

    printf("Jobs executados: %d\n", jobs_executados);

    printf("Orcamento: %.3f ms\n", Cs / 1000000.0);

    printf("Consumido: %.3f ms\n", consumed_ns / 1000000.0);

    if (consumed_ns <= Cs) {
      printf("Restante: %.3f ms\n", (Cs - consumed_ns) / 1000000.0);
    }else{
      printf("Ultrapassagem: %.3f ms\n", (consumed_ns - Cs) / 1000000.0);
    }
    printf("===============================\n");

    end_of_service:
    /////////////////////// Dorme até o instante absoluto da próxima liberação
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_release, NULL);

    // Mede o intervalo entre as ativações
    struct timespec agora;
    clock_gettime(CLOCK_MONOTONIC, &agora);

    long intervalo =
      (agora.tv_sec - ultimo_inicio.tv_sec) * 1000000L +
      (agora.tv_nsec - ultimo_inicio.tv_nsec) / 1000L;

    printf("Intervalo entre ativacoes: %ld us\n", intervalo);

    ultimo_inicio = agora;
  }

  return NULL;
}

void start_server_thread() {
  pthread_t th;
  pthread_attr_t attr;
  struct sched_param sp;

  pthread_attr_init(&attr);
  pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
  pthread_attr_setschedpolicy(&attr, SCHED_FIFO); // ou SCHED_RR
  sp.sched_priority = 60; // prioridade RT (ajuste conforme sistema)
  pthread_attr_setschedparam(&attr, &sp);

  static server_params_t params = {
  .period_ns = 10L * 1000000L, // Ts = 10 ms
  .budget_ns = 3L * 1000000L // Cs = 3 ms por período
  };

  pthread_create(&th, &attr, server_thread, &params);
  pthread_attr_destroy(&attr);
}

void meu_job(void *arg) {
  /////////////////////// código que representa a tarefa aperiódica
  printf("Processando requisição aperiódica %d\n", *(int*)arg);
  free(arg);
  /////////////////////// ...
}

// Simula o processamento do desviador durante 700 us
void cpu_tight_loop_us(long us) {
  struct timespec inicio, atual;

  clock_gettime(CLOCK_MONOTONIC, &inicio);

  do{
    clock_gettime(CLOCK_MONOTONIC, &atual);

    long decorrido =
      (atual.tv_sec - inicio.tv_sec) * 1000000L +
      (atual.tv_nsec - inicio.tv_nsec) / 1000L;

    if (decorrido >= us) break;

  }while(1);
}

// Representa a tarefa SORT_ACT da M2
void novo_sort(void *arg) {
  sort_evt_t *ev = (sort_evt_t *)arg;

  struct timespec inicio, fim;

  clock_gettime(CLOCK_MONOTONIC, &inicio);

  // Tempo entre a requisição e o início do processamento
  long espera =
      (inicio.tv_sec - ev->instante_evento.tv_sec) * 1000000L +
      (inicio.tv_nsec - ev->instante_evento.tv_nsec) / 1000L;

  // Simula a preparação do desviador
  cpu_tight_loop_us(700);

  clock_gettime(CLOCK_MONOTONIC, &fim);

  // Tempo de execução
  long execucao =
      (fim.tv_sec - inicio.tv_sec) * 1000000L +
      (fim.tv_nsec - inicio.tv_nsec) / 1000L;

  // Tempo total de resposta
  long resposta = espera + execucao;

  printf(
    "SORT_ACT %d | Espera: %ld us | Execucao: %ld us | Resposta: %ld us\n",
    ev->id, espera, execucao, resposta
  );

  if (resposta > 10000) {
    printf("DEADLINE EXCEDIDO!\n");
  }

  free(ev);
}

// Executa uma tarefa periodica
void *periodic_thread(void *arg) {

  periodic_params_t *params = (periodic_params_t *)arg;

  long periodo_ns = params->periodo_ms * 1000000L;

  struct timespec proxima_ativacao;
  clock_gettime(CLOCK_MONOTONIC, &proxima_ativacao);

  int execucoes = 0;

  while (1) {
    // Aguarda a proxima ativacao
    clock_nanosleep(
      CLOCK_MONOTONIC,
      TIMER_ABSTIME,
      &proxima_ativacao,
      NULL
    );

    // Instante previsto para a ativacao
    struct timespec liberacao = proxima_ativacao;

    // Inicio real da execucao
    struct timespec inicio, fim;

    clock_gettime(CLOCK_MONOTONIC, &inicio);

    // Tempo de espera entre liberacao e inicio
    long espera =
      (inicio.tv_sec - liberacao.tv_sec) * 1000000L +
      (inicio.tv_nsec - liberacao.tv_nsec) / 1000L;

    // Simula o processamento da tarefa
    cpu_tight_loop_us(params->execucao_us);

    clock_gettime(CLOCK_MONOTONIC, &fim);

    // Tempo de execucao
    long execucao =
      (fim.tv_sec - inicio.tv_sec) * 1000000L +
      (fim.tv_nsec - inicio.tv_nsec) / 1000L;

    // Tempo de resposta
    long resposta = espera + execucao;

    execucoes++;

    printf(
      "%s | Execucao: %d | Espera: %ld us | Execucao: %ld us | Resposta: %ld us\n",
      params->nome,
      execucoes,
      espera,
      execucao,
      resposta
    );

    // Verifica deadline
    if (resposta > params->periodo_ms * 1000L) {
      printf("%s | DEADLINE EXCEDIDO!\n", params->nome);
    }

    // Agenda a proxima ativacao
    timespec_add_ns(&proxima_ativacao, periodo_ns);
  }

  return NULL;
}

void start_periodic_tasks() {

  pthread_t threads[4];
  pthread_attr_t attr;
  struct sched_param sp;

  // Configura escalonamento em tempo real
  pthread_attr_init(&attr);

  pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
  pthread_attr_setschedpolicy(&attr, SCHED_FIFO);

  // Prioridade maior que a do servidor (60)
  sp.sched_priority = 70;

  pthread_attr_setschedparam(&attr, &sp);

  // Parametros das quatro tarefas
  static periodic_params_t tarefas[4] = {
    {"T1", 2, 200},
    {"T2", 5, 200},
    {"T3", 10, 200},
    {"T4", 20, 200}
  };

  // Cria as quatro threads
  for (int i = 0; i < 4; i++) {

    pthread_create(
      &threads[i],
      &attr,
      periodic_thread,
      &tarefas[i]
    );
  }

  pthread_attr_destroy(&attr);
}

/*
void requisicao_aperiodica(int id) {
  int *p = malloc(sizeof(int));
  *p = id;
  enqueue_job(meu_job, p);
}
*/

void requisicao_aperiodica(int id) {
  sort_evt_t *ev = malloc(sizeof(sort_evt_t));

  if (ev == NULL) {
    perror("malloc");
    return;
  }

  ev->id = id;

  clock_gettime(CLOCK_MONOTONIC, &ev->instante_evento);

  enqueue_job(novo_sort, ev);
}

/////////////////////// GERADOR DE REQUISIÇÕES APERIÓDICAS ///////////////////////

void *aperiodic_generator(void *arg) {
    (void)arg;
    int counter = 0;

    while (1) {
        // Gera um job a cada 30–120 ms
        int delay_ms = 30 + rand() % 91;

        struct timespec req = {0};
        req.tv_nsec = delay_ms * 1000000L;
        nanosleep(&req, NULL);

        sort_evt_t *ev = malloc(sizeof(sort_evt_t));

        if (ev == NULL)
            continue;

        ev->id = ++counter;
        clock_gettime(CLOCK_MONOTONIC, &ev->instante_evento);

        printf("[GERADOR] nova requisicao aperiodica %d\n", ev->id);

        enqueue_job(novo_sort, ev);
    }

    return NULL;
}

int main(){
  //cria 20 requisições do sort
  int i;
  for (i = 0; i < 20; i++) {
    requisicao_aperiodica(i);
  }

  // Inicia as tarefas periodicas
  start_periodic_tasks();

  //inicia o servidor que le as requisições
  start_server_thread();

  // Inicia o gerador de requisicoes aperiodicas
  pthread_t generator;
  pthread_create(&generator, NULL, aperiodic_generator, NULL);

  while (1) {
    pause();
  }
  return 0;
}
