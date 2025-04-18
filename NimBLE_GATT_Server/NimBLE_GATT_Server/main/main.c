/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
/* Includes */
#include "common.h"
#include "gap.h"
#include "gatt_svc.h"
#include "led.h"
#include "esp_spiffs.h"
#include "esp_vfs.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include <inttypes.h>

/* Library function declarations */
void ble_store_config_init(void);

/* Private function declarations */
static void on_stack_reset(int reason);
static void on_stack_sync(void);
static void nimble_host_config_init(void);
static void nimble_host_task(void *param);

/* Private functions */
/*
 *  Stack event callback functions
 *      - on_stack_reset is called when host resets BLE stack due to errors
 *      - on_stack_sync is called when host has synced with controller
 */
static void on_stack_reset(int reason) {
    /* On reset, print reset reason to console */
    ESP_LOGI(TAG, "nimble stack reset, reset reason: %d", reason);
}

static void on_stack_sync(void) {
    /* On stack sync, do advertising initialization */
    adv_init();
}

static void nimble_host_config_init(void) {
    /* Set host callbacks */
    ble_hs_cfg.reset_cb = on_stack_reset;
    ble_hs_cfg.sync_cb = on_stack_sync;
    ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    /* Store host configuration */
    ble_store_config_init();
}

static void nimble_host_task(void *param) {
    /* Task entry log */
    ESP_LOGI(TAG, "nimble host task has been started!");

    /* This function won't return until nimble_port_stop() is executed */
    nimble_port_run();

    /* Clean up at exit */
    vTaskDelete(NULL);
}

static void check_valid_partition(void *param)
{
    esp_partition_t* otaApp = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, "ota_0");
    uint8_t sha = 0;

    
    if (otaApp == NULL)
    {
        printf("\n NULL \n");
    }
    else
    {

        printf("\nSuccess!\n");
    }


    if (esp_partition_get_sha256(otaApp,&sha) == ESP_OK)
    {
        printf("GOT THE SHA!!");
        esp_err_t err = esp_ota_set_boot_partition(otaApp);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG,"Failed to set boot partition,returning to bluetooth...\n");
            vTaskDelete(NULL);
        }
    
        ESP_LOGI(TAG,"Everything went well now we are restarting!");
        printf("\nI am here right before restart");
        esp_restart();
    }

    vTaskDelete(NULL);

}



void app_main(void) {
    /*
    const esp_partition_t *partition = NULL;

    // Check each partition and pass NULL for the label if not needed
    partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    if (partition != NULL) {
        ESP_LOGI("Partition", "app0 offset: 0x%" PRIX32 " size: 0x%" PRIu32, partition->address, partition->size);
    }

    partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
    if (partition != NULL) {
        ESP_LOGI("Partition", "app1 offset: 0x%" PRIX32 " size: 0x%" PRIu32, partition->address, partition->size);
    }

    partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
    if (partition != NULL) {
        ESP_LOGI("Partition", "otadata offset: 0x%" PRIX32 " size: 0x%" PRIu32, partition->address, partition->size);
    }

    partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, NULL);
    if (partition != NULL) {
        ESP_LOGI("Partition", "nvs offset: 0x%" PRIX32 " size: 0x%" PRIu32, partition->address, partition->size);
    }

    partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL);
    if (partition != NULL) {
        ESP_LOGI("Partition", "spiffs offset: 0x%" PRIX32 " size: 0x%" PRIu32, partition->address, partition->size);
    }
    */
   
    /* Local variables */
    int rc;
    esp_err_t ret;

    /* LED initialization */
    led_init();

    /* Mount SPIFFS */
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true
    };

    ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount or format SPIFFS (%s)",
                 esp_err_to_name(ret));
        return;
    } else {
        size_t total = 0, used = 0;
        esp_spiffs_info(NULL, &total, &used);
        ESP_LOGI(TAG, "SPIFFS mounted. Total: %d bytes, Used: %d bytes",
                 total, used);
    }

    /* Optional: Check if file exists, or create it */
    FILE *fp = fopen("/spiffs/upload.txt", "a+");
    if (fp) {
        fclose(fp);
        ESP_LOGI(TAG, "Ensured upload.txt exists");
    } else {
        ESP_LOGE(TAG, "Could not create initial file");
    }


    /*
     * NVS flash initialization
     * Dependency of BLE stack to store configurations
     */
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize nvs flash, error code: %d ", ret);
        return;
    }

    /* NimBLE stack initialization */
    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize nimble stack, error code: %d ",
                 ret);
        return;
    }

    /* GAP service initialization */
    rc = gap_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "failed to initialize GAP service, error code: %d", rc);
        return;
    }

    /* GATT server initialization */
    rc = gatt_svc_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "failed to initialize GATT server, error code: %d", rc);
        return;
    }

    /* NimBLE host configuration initialization */
    nimble_host_config_init();

    /* Start NimBLE host task thread and return */
    //xTaskCreate(check_valid_partition, "Partition Checki", 4*1024, NULL, 5, NULL);
    xTaskCreate(nimble_host_task, "NimBLE Host", 4*1024, NULL, 5, NULL);
    // while(1)
    // {
    //     printf("\nYO");
    //     vTaskDelay(1000/portTICK_PERIOD_MS);
    // }
    return;
}
