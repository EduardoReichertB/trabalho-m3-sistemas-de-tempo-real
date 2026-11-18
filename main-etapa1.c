
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#define TAM 1024
#define AMOSTRAS 1000
#define LETRAS 26
#define MAX_NOS 51
#define MAX_BITS (TAM * 25)
#define SEMENTE 12345

typedef struct {
  int frequencia;
  int esquerdo, direito, pai;
  int letra;
} No;

static No arvore[MAX_NOS];
static int folhas[LETRAS];
static int raiz;

static uint8_t entrada[TAM];
static uint8_t decodificada[TAM];
static uint8_t comprimida[(MAX_BITS + 7) / 8];

// 1. Construir a arvore de Huffman
void construir_arvore(void) {
  int frequencias[LETRAS] = {0};
  int quantidade = 0;

  for (int i = 0; i < TAM; i++) {
    frequencias[entrada[i] - 'A']++;
  }

  for (int i = 0; i < LETRAS; i++) {
    if (frequencias[i] > 0) {
      arvore[quantidade] = (No){frequencias[i], -1, -1, -1, i};
      folhas[i] = quantidade++;
    }
  }

  while (1) {
    int menor1 = -1;
    int menor2 = -1;

    for (int i = 0; i < quantidade; i++) {
      if (arvore[i].pai != -1)
        continue;

      if (menor1 == -1 || arvore[i].frequencia < arvore[menor1].frequencia) {
        menor2 = menor1;
        menor1 = i;
      }else if (menor2 == -1 || arvore[i].frequencia < arvore[menor2].frequencia) {
        menor2 = i;
      }
    }

    if (menor2 == -1) {
      raiz = menor1;
      return;
    }

    // Criar um novo nó com a soma das frequências
    arvore[quantidade].frequencia = arvore[menor1].frequencia + arvore[menor2].frequencia;

    // Definir os filhos
    arvore[quantidade].esquerdo = menor1;
    arvore[quantidade].direito = menor2;

    // O novo nó ainda não possui pai e não representa uma letra
    arvore[quantidade].pai = -1;
    arvore[quantidade].letra = -1;

    // Atualizar o pai dos dois nós selecionados
      arvore[menor1].pai = quantidade;
      arvore[menor2].pai = quantidade;

      // Atualizar a quantidade de nós existentes
      quantidade++;
  }
}

// 2. Codificar os caracteres

int codificar(void) {
  int quantidade_bits = 0;

  // Limpar o vetor comprimido
  memset(comprimida, 0, sizeof(comprimida));

  // Percorrer todos os caracteres da mensagem
  for (int i = 0; i < TAM; i++) {
    int caminho[LETRAS];
    int tamanho = 0;

    // Encontrar o nó correspondente ao caractere
    int atual = folhas[entrada[i] - 'A'];

    // Percorrer da folha até a raiz
    while (arvore[atual].pai != -1) {
      int pai = arvore[atual].pai;

      // Identificar se o caminho é direita ou esquerda
      if (arvore[pai].direito == atual) {
        caminho[tamanho] = 1;
      } else {
        caminho[tamanho] = 0;
      }

      tamanho++;
      atual = pai;
    }

    // Caso exista apenas uma letra distinta
    if (tamanho == 0) {
      caminho[0] = 0;
      tamanho = 1;
    }

    // Gravar os bits na ordem correta
    for (int j = tamanho - 1; j >= 0; j--) {
      int bit = caminho[j];

      int byte = quantidade_bits / 8;
      int deslocamento = 7 - (quantidade_bits % 8);

      if (bit == 1) {
        comprimida[byte] = comprimida[byte] | (1 << deslocamento);
      }
      quantidade_bits++;
    }
  }

  return quantidade_bits;
}


// 3. Decodificar os caracteres

int decodificar(int quantidade_bits) {
  int atual = raiz;
  int posicao = 0;

  // Caso exista somente uma letra distinta
  if (arvore[raiz].letra != -1) {
    if (quantidade_bits != TAM) {
      return -1;
    }

    for (int i = 0; i < TAM; i++) {
      decodificada[i] = 'A' + arvore[raiz].letra;
    }

    return TAM;
  }

  // Percorrer todos os bits comprimidos
  for (int i = 0; i < quantidade_bits; i++) {
    // Encontrar o byte e a posição do bit
    int byte = i / 8;
    int deslocamento = 7 - (i % 8);

    // Ler o bit
    int bit = (comprimida[byte] >> deslocamento) & 1;

    // Percorrer a árvore
    if (bit == 0) {
      atual = arvore[atual].esquerdo;
    }else {
      atual = arvore[atual].direito;
    }

    // Verificar se encontramos uma letra
    if (arvore[atual].letra != -1) {
      if (posicao >= TAM) {
        return -1;
      }

      // Armazenar a letra decodificada
      decodificada[posicao] ='A' + arvore[atual].letra;

      posicao++;
      // Voltar à raiz para decodificar a próxima letra
      atual = raiz;
    }
  }

  // Verificar se o percurso terminou corretamente
  if (atual != raiz) {
    return -1;
  }

  return posicao;
}


// 4. Executar Huffman e validar a saida
int huffman_validar(void) {
  construir_arvore();

  int quantidade_bits = codificar();
  int tamanho = decodificar(quantidade_bits);

  return tamanho == TAM && memcmp(entrada, decodificada, TAM) == 0;
}

// 5. Etapa 1 - Medicao de tempo
void app_main(void) {
  srand(SEMENTE);

  // Gerar mensagem aleatoria
  for (int i = 0; i < TAM; i++) {
    entrada[i] = (uint8_t)('A' + rand() % LETRAS);
  }

  double minimo = 1e30;
  double maximo = 0;
  double soma = 0;

  printf("Tamanho=%d Semente=%d Amostras=%d\n", TAM, SEMENTE, AMOSTRAS);

  printf("CSV_INICIO\n");
  printf("ativacao,tempo_us\n");

  for (int i = 1; i <= AMOSTRAS; i++) {
    struct timespec inicio, fim;

    clock_gettime(CLOCK_MONOTONIC, &inicio);

    int valido = huffman_validar();

    clock_gettime(CLOCK_MONOTONIC, &fim);

    if (!valido) {
      printf("ERRO: validacao falhou na ativacao %d\n", i);
      return;
    }

    double tempo_us = (fim.tv_sec - inicio.tv_sec) * 1000000.0 + (fim.tv_nsec - inicio.tv_nsec) / 1000.0;

    if (tempo_us < minimo)
      minimo = tempo_us;

    if (tempo_us > maximo)
      maximo = tempo_us;

    soma += tempo_us;

    printf("%d,%.3f\n", i, tempo_us);
  }

  printf("CSV_FIM\n");

  printf("\n--- RESULTADOS ---\n");
  printf("Minimo: %.3f us\n", minimo);
  printf("Media: %.3f us\n", soma / AMOSTRAS);
  printf("HWM: %.3f us\n", maximo);
  printf("Deadline sugerido: %.3f us\n", maximo * 1.2);
  printf("Validacao: OK\n");
}
