
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_timer.h"
#include "esp_err.h"
#include "driver/touch_sens.h"
#include "esp_attr.h"

#define TAM 1024
#define LETRAS 26
#define MAX_NOS 51
#define MAX_BITS (TAM * 25)
#define SEMENTE 12345

static touch_sensor_handle_t touchHandle = NULL;

// Configuracao do experimento
#define TOTAL_AMOSTRAS 1000
#define DEADLINE_US 4905.6

// TOUCH0 = GPIO4 no ESP32 original
#define TOUCH_PIN TOUCH_PAD_NUM0

// ATENCAO: calibrar este valor conforme a leitura do sensor
#define TOUCH_THRESHOLD 400

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

// Tarefas
static TaskHandle_t huffmanTaskHandle = NULL;
static TaskHandle_t touchTaskHandle = NULL;

// Sincronizacao
static SemaphoreHandle_t touchSemaphore;

// Resultados das medicoes
static uint64_t tempos_us[TOTAL_AMOSTRAS];

static uint32_t touch_events = 0;

static portMUX_TYPE touchMux =
    portMUX_INITIALIZER_UNLOCKED;

// =====================================================
// 1. CONSTRUIR ARVORE
// =====================================================

void construir_arvore(void) {

    int frequencias[LETRAS] = {0};
    int quantidade = 0;

    for (int i = 0; i < TAM; i++) {
        frequencias[entrada[i] - 'A']++;
    }

    for (int i = 0; i < LETRAS; i++) {
        if (frequencias[i] > 0) {
            arvore[quantidade] =
                (No){frequencias[i], -1, -1, -1, i};

            folhas[i] = quantidade++;
        }
    }

    while (1) {

        int menor1 = -1;
        int menor2 = -1;

        for (int i = 0; i < quantidade; i++) {

            if (arvore[i].pai != -1)
                continue;

            if (menor1 == -1 ||
                arvore[i].frequencia <
                arvore[menor1].frequencia) {

                menor2 = menor1;
                menor1 = i;

            } else if (menor2 == -1 ||
                       arvore[i].frequencia <
                       arvore[menor2].frequencia) {

                menor2 = i;
            }
        }

        if (menor2 == -1) {
            raiz = menor1;
            return;
        }

        arvore[quantidade].frequencia =
            arvore[menor1].frequencia +
            arvore[menor2].frequencia;

        arvore[quantidade].esquerdo = menor1;
        arvore[quantidade].direito = menor2;

        arvore[quantidade].pai = -1;
        arvore[quantidade].letra = -1;

        arvore[menor1].pai = quantidade;
        arvore[menor2].pai = quantidade;

        quantidade++;
    }
}

// =====================================================
// 2. CODIFICAR
// =====================================================

int codificar(void) {

    int quantidade_bits = 0;

    memset(comprimida, 0, sizeof(comprimida));

    for (int i = 0; i < TAM; i++) {

        int caminho[LETRAS];
        int tamanho = 0;

        int atual = folhas[entrada[i] - 'A'];

        while (arvore[atual].pai != -1) {

            int pai = arvore[atual].pai;

            if (arvore[pai].direito == atual)
                caminho[tamanho] = 1;
            else
                caminho[tamanho] = 0;

            tamanho++;
            atual = pai;
        }

        if (tamanho == 0) {
            caminho[0] = 0;
            tamanho = 1;
        }

        for (int j = tamanho - 1; j >= 0; j--) {

            int bit = caminho[j];

            int byte = quantidade_bits / 8;
            int deslocamento = 7 - (quantidade_bits % 8);

            if (bit == 1) {
                comprimida[byte] |= (1 << deslocamento);
            }

            quantidade_bits++;
        }
    }

    return quantidade_bits;
}

// =====================================================
// 3. DECODIFICAR
// =====================================================

int decodificar(int quantidade_bits) {

    int atual = raiz;
    int posicao = 0;

    if (arvore[raiz].letra != -1) {

        if (quantidade_bits != TAM)
            return -1;

        for (int i = 0; i < TAM; i++) {
            decodificada[i] = 'A' + arvore[raiz].letra;
        }

        return TAM;
    }

    for (int i = 0; i < quantidade_bits; i++) {

        int byte = i / 8;
        int deslocamento = 7 - (i % 8);

        int bit =
            (comprimida[byte] >> deslocamento) & 1;

        if (bit == 0)
            atual = arvore[atual].esquerdo;
        else
            atual = arvore[atual].direito;

        if (arvore[atual].letra != -1) {

            if (posicao >= TAM)
                return -1;

            decodificada[posicao] =
                'A' + arvore[atual].letra;

            posicao++;

            atual = raiz;
        }
    }

    if (atual != raiz)
        return -1;

    return posicao;
}

// =====================================================
// 4. VALIDAR HUFFMAN
// =====================================================

int huffman_validar(void) {

    construir_arvore();

    int quantidade_bits = codificar();

    int tamanho = decodificar(quantidade_bits);

    return tamanho == TAM &&
           memcmp(entrada, decodificada, TAM) == 0;
}

// =====================================================
// 5. INTERRUPCAO DO TOUCH
// =====================================================

static bool touch_callback(
    touch_sensor_handle_t sens_handle,
    const touch_hw_active_event_data_t *event,
    void *user_ctx
) {
    BaseType_t acordou = pdFALSE;

    xSemaphoreGiveFromISR(
        touchSemaphore,
        &acordou
    );

    return acordou == pdTRUE;
}

// =====================================================
// 6. TAREFA TOUCH
// =====================================================

void TouchTask(void *pvParameters) {

    printf("TouchTask iniciada. Aguardando toques...\n");

    while (1) {

        // Aguarda uma interrupcao do TOUCH
        xSemaphoreTake(
            touchSemaphore,
            portMAX_DELAY
        );

        // Contabiliza o evento
        portENTER_CRITICAL(&touchMux);
        touch_events++;
        uint32_t total = touch_events;
        portEXIT_CRITICAL(&touchMux);

        printf("TOUCH detectado! Total de eventos: %lu\n",
               (unsigned long)total);
    }
}

// =====================================================
// 7. TAREFA HUFFMAN
// =====================================================

void HuffmanTask(void *pvParameters) {

    double minimo = 1e30;
    double maximo = 0;
    double soma = 0;

    int falhas = 0;
    int deadlines_excedidos = 0;

    printf("\nHuffmanTask iniciada!\n");
    printf("Iniciando %d medicoes...\n", TOTAL_AMOSTRAS);

    for (int i = 0; i < TOTAL_AMOSTRAS; i++) {

        // Inicio da medicao
        int64_t inicio = esp_timer_get_time();

        // Executar Huffman e validar
        int valido = huffman_validar();

        // Fim da medicao
        int64_t fim = esp_timer_get_time();

        uint64_t tempo_us = fim - inicio;

        // Armazenar resultado
        tempos_us[i] = tempo_us;

        // Validacao
        if (!valido) {
            falhas++;
        }

        // Estatisticas
        if (tempo_us < minimo) {
            minimo = (double)tempo_us;
        }

        if (tempo_us > maximo) {
            maximo = (double)tempo_us;
        }

        soma += (double)tempo_us;

        // Verificar deadline
        if (tempo_us > DEADLINE_US) {
            deadlines_excedidos++;
        }
    }

    // =========================================
    // CSV
    // =========================================

    printf("\nCSV_INICIO\n");
    printf("ativacao,tempo_us\n");

    for (int i = 0; i < TOTAL_AMOSTRAS; i++) {

        printf("%d,%llu\n",
               i + 1,
               (unsigned long long)tempos_us[i]);
    }

    printf("CSV_FIM\n");

    // =========================================
    // RESULTADOS
    // =========================================

    printf("\n--- RESULTADOS ---\n");

    printf("Minimo: %.3f us\n", minimo);

    printf("Media: %.3f us\n",
           soma / TOTAL_AMOSTRAS);

    printf("HWM: %.3f us\n", maximo);

    printf("Deadline: %.3f us\n", DEADLINE_US);

    printf("Deadlines excedidos: %d\n",
           deadlines_excedidos);

    printf("Falhas de validacao: %d\n", falhas);

    printf("Validacao: %s\n",
           falhas == 0 ? "OK" : "FALHOU");

    printf("Total de medicoes: %d\n", TOTAL_AMOSTRAS);

    printf("--- FIM DO EXPERIMENTO ---\n");

    vTaskDelete(NULL);
}

// =====================================================
// 8. INICIALIZAR TOUCH
// =====================================================

void inicializar_touch(void) {

    // Configuracao da amostragem
    touch_sensor_sample_config_t sample_cfg =
        TOUCH_SENSOR_V1_DEFAULT_SAMPLE_CONFIG(
            5.0,
            TOUCH_VOLT_LIM_L_0V5,
            TOUCH_VOLT_LIM_H_1V7
        );

    // Configuracao do controlador
    touch_sensor_config_t touch_cfg =
        TOUCH_SENSOR_DEFAULT_BASIC_CONFIG(
            1,
            &sample_cfg
        );

    // Interrupcao quando o valor ficar abaixo do threshold
    touch_cfg.intr_trig_mode =
        TOUCH_INTR_TRIG_ON_BELOW_THRESH;

    touch_cfg.intr_trig_group =
        TOUCH_INTR_TRIG_GROUP_BOTH;

    // Criar controlador
    ESP_ERROR_CHECK(
        touch_sensor_new_controller(
            &touch_cfg,
            &touchHandle
        )
    );

    // Configurar canal TOUCH0 (GPIO4)
    touch_channel_config_t chan_cfg = {
        .abs_active_thresh = {1000},
        .charge_speed = TOUCH_CHARGE_SPEED_7,
        .init_charge_volt = TOUCH_INIT_CHARGE_VOLT_DEFAULT,
        .group = TOUCH_CHAN_TRIG_GROUP_BOTH
    };

    touch_channel_handle_t channelHandle = NULL;

    ESP_ERROR_CHECK(
        touch_sensor_new_channel(
            touchHandle,
            0,
            &chan_cfg,
            &channelHandle
        )
    );

    // Registrar callback da interrupcao
    touch_event_callbacks_t callbacks = {
        .on_hw_active = touch_callback
    };

    ESP_ERROR_CHECK(
        touch_sensor_register_callbacks(
            touchHandle,
            &callbacks,
            NULL
        )
    );

    // Habilitar TOUCH
    ESP_ERROR_CHECK(
        touch_sensor_enable(touchHandle)
    );

    // Iniciar leitura continua
    ESP_ERROR_CHECK(
        touch_sensor_start_continuous_scanning(
            touchHandle
        )
    );

    printf("TOUCH inicializado!\n");
}

// =====================================================
// 9. APP_MAIN
// =====================================================

void app_main(void) {

    srand(SEMENTE);

    // Gerar mensagem de entrada
    for (int i = 0; i < TAM; i++) {
        entrada[i] = 'A' + rand() % LETRAS;
    }

    // Criar semaforo contador para os eventos TOUCH
    touchSemaphore = xSemaphoreCreateCounting(1000, 0);

    if (touchSemaphore == NULL) {
        printf("Erro ao criar semaforo TOUCH!\n");
        return;
    }

    // Inicializar hardware TOUCH
    inicializar_touch();

    printf("\n================================\n");
    printf(" EXPERIMENTO ETAPA 2\n");
    printf("================================\n");

    printf("Amostras: %d\n", TOTAL_AMOSTRAS);
    printf("Deadline: %.3f us\n", DEADLINE_US);

    printf("Huffman: Core 1\n");
    printf("Touch: Core 0\n");

    // Criar tarefa TOUCH no Core 0
    BaseType_t touchCriada = xTaskCreatePinnedToCore(
        TouchTask,
        "TouchTask",
        4096,
        NULL,
        5,
        &touchTaskHandle,
        0
    );

    // Criar tarefa HUFFMAN no Core 1
    BaseType_t huffmanCriada = xTaskCreatePinnedToCore(
        HuffmanTask,
        "HuffmanTask",
        8192,
        NULL,
        5,
        &huffmanTaskHandle,
        1
    );

    if (touchCriada != pdPASS ||
        huffmanCriada != pdPASS) {

        printf("Erro ao criar as tarefas!\n");
        return;
    }

    printf("\nSistema iniciado!\n");
}
