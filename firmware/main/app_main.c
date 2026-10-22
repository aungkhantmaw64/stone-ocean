#include "esp_app_desc.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "boot_sequence.h"

#if CONFIG_NODE_MESH_ENABLE
#define BUILD_FLAVOUR "hw"
#else
#define BUILD_FLAVOUR "qemu"
#endif

static const char *TAG = "app_main";

void app_main(void) {
  const esp_app_desc_t *app = esp_app_get_description();

  ESP_LOGI(TAG, "%s %s | IDF %s | flavour %s", app->project_name, app->version,
           esp_get_idf_version(), BUILD_FLAVOUR);

  boot_sequence_run();
}
