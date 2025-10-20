#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_task_wdt.h"

#define NOME_RM "{Pedro Galante Branco - RM:88992}"

static QueueHandle_t data_queue = NULL;
#define QUEUE_LENGTH 3  // pequena para gerar overflow facilmente
#define QUEUE_ITEM_SIZE sizeof(int)

static EventGroupHandle_t status_eg;
#define BIT_PRODUCER_OK   (1<<0)
#define BIT_CONSUMER_OK   (1<<1)
#define BIT_SUPERVISOR_OK (1<<2)

#define TWDT_TIMEOUT_S 5  // curto para demonstrar reinicializacao

static TaskHandle_t producer_handle = NULL;
static TaskHandle_t consumer_handle = NULL;
static TaskHandle_t supervisor_handle = NULL;

// GERADOR
static void producer_task(void *arg)
{
    esp_task_wdt_add(NULL);

    int counter = 0;
    for (;;)
    {
        BaseType_t res = xQueueSend(data_queue, &counter, 0);
        if (res == pdPASS) {
            printf("%s [FILA] Dado enviado com sucesso! valor=%d\n", NOME_RM, counter);
        } else {
            printf("%s [FILA] FILA CHEIA: dado descartado valor=%d\n", NOME_RM, counter);
        }
        xEventGroupSetBits(status_eg, BIT_PRODUCER_OK);

        counter++;
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// RECEPTOR
static void consumer_task(void *arg)
{
    esp_task_wdt_add(NULL);

    const TickType_t rx_timeout = pdMS_TO_TICKS(1000);
    int timeout_count = 0;
    int forced_error = 0;

    for (;;)
    {
        int value;
        BaseType_t received = xQueueReceive(data_queue, &value, rx_timeout);

        // Força o timeout e falhas para demonstrar todas as mensagens 
        if (forced_error == 0) {
            // Recebe normalmente 1 vez
            if (received == pdPASS) {
                int *pval = (int *)malloc(sizeof(int));
                if (pval) {
                    *pval = value;
                    printf("%s [RECEPTOR] Dado recebido e transmitido: %d\n", NOME_RM, *pval);
                    free(pval);
                    xEventGroupSetBits(status_eg, BIT_CONSUMER_OK);
                }
            }
            forced_error = 1;
            continue;
        }

        if (forced_error == 1) {
            // Simula timeout -> AVISO
            timeout_count++;
            printf("%s [RECEPTOR] AVISO: sem dados recebidos no timeout (tentativa 1)\n", NOME_RM);
            xEventGroupClearBits(status_eg, BIT_CONSUMER_OK);
            forced_error = 2;
            continue;
        }

        if (forced_error == 2) {
            // Simula RECUPERAÇÃO
            printf("%s [RECEPTOR] RECUPERACAO: tentativa de reset da fila e reentrada\n", NOME_RM);
            xQueueReset(data_queue);
            vTaskDelay(pdMS_TO_TICKS(500));
            forced_error = 3;
            continue;
        }

        if (forced_error == 3) {
            // Simula FALHA CRÍTICA e espera WDT
            printf("%s [RECEPTOR] FALHA CRITICA: sem dados apos varias tentativas. Esperando reinicializacao pelo WDT.\n", NOME_RM);
            xEventGroupClearBits(status_eg, BIT_CONSUMER_OK);
            vTaskDelay(pdMS_TO_TICKS((TWDT_TIMEOUT_S + 2) * 1000)); // Bloqueio para provocar WDT
            forced_error = 4;
            continue;
        }

        // Reset WDT se nada crítico
        esp_task_wdt_reset();
    }
}

// SUPERVISOR
static void supervisor_task(void *arg)
{
    esp_task_wdt_add(NULL);

    for (;;)
    {
        EventBits_t bits = xEventGroupGetBits(status_eg);

        const char *prod_status = (bits & BIT_PRODUCER_OK) ? "OK" : "NOK";
        const char *cons_status = (bits & BIT_CONSUMER_OK) ? "OK" : "NOK";

        printf("%s [SUPERVISOR] Status -> Producer: %s | Consumer: %s\n", NOME_RM, prod_status, cons_status);

        if (!(bits & BIT_PRODUCER_OK)) {
            printf("%s [SUPERVISOR] ALERTA: produtor detectado como NOK\n", NOME_RM);
        }
        if (!(bits & BIT_CONSUMER_OK)) {
            printf("%s [SUPERVISOR] ALERTA: consumidor detectado como NOK\n", NOME_RM);
        }

        xEventGroupSetBits(status_eg, BIT_SUPERVISOR_OK);
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

void app_main(void)
{
    // Detecta causa da reinicialização 
    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_TASK_WDT) {
        printf("%s [SYSTEM] Reinicializacao anterior causada pelo Watchdog Timer!\n", NOME_RM);
    } else {
        printf("%s [SYSTEM] Inicializacao normal do sistema.\n", NOME_RM);
    }

    printf("%s [SYSTEM] Inicializando sistema multitarefa (FreeRTOS) e TWDT...\n", NOME_RM);

    data_queue = xQueueCreate(QUEUE_LENGTH, QUEUE_ITEM_SIZE);
    status_eg = xEventGroupCreate();

    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = TWDT_TIMEOUT_S * 1000,
        .idle_core_mask = (1<<0) | (1<<1),
        .trigger_panic = true
    };
    esp_task_wdt_init(&wdt_config);

     // Criar tarefas 
    xTaskCreate(producer_task, "producer_task", 4096, NULL, 5, &producer_handle);
    xTaskCreate(consumer_task, "consumer_task", 4096, NULL, 5, &consumer_handle);
    xTaskCreate(supervisor_task, "supervisor_task", 4096, NULL, 6, &supervisor_handle);

    printf("%s [SYSTEM] Tarefas criadas. Sistema em execucao.\n", NOME_RM);
}
