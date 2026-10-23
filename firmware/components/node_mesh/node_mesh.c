/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include "node_mesh.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_health_model_api.h"
#include "esp_ble_mesh_local_data_operation_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_err.h"
#include "node_config.h"
#include "node_hal.h"
#include "node_health.h"
#include "node_mesh_comp.h"
#include "node_mesh_err.h"
#include "node_mesh_internal.h"
#include "node_mesh_log.h"
#include "node_mesh_port.h"

/* ESP-BLE-MESH internal header. No public API gives the IV index or lists the
 * key indexes, which spec 10 logs at boot, so they are read from the stack's
 * bt_mesh state. This ties node_mesh to the pinned ESP-IDF v5.4.4. */
#include "net.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Company ID in the composition data: Espressif (spec 10). */
#define NODE_MESH_CID_ESPRESSIF 0x02E5

/** @brief Default TTL of messages the node sends. */
#define NODE_MESH_DEFAULT_TTL 7

/** @brief Network and relay retransmissions: 3 transmissions, 20 ms apart,
 *         as in the ESP-BLE-MESH examples. */
#define NODE_MESH_TRANSMIT ESP_BLE_MESH_TRANSMIT(2, 20)

/** @brief Free internal RAM after the mesh start below which the start is
 *         logged as an error, in bytes (spec 10). */
#define NODE_MESH_MIN_FREE_INTERNAL_BYTES (40u * 1024u)

/** @brief Faults the Health Server can report at once. */
#define NODE_MESH_HEALTH_MAX_FAULTS 4

/** @brief Time between checks that the mesh reset of a factory reset has
 *         finished, in ms. */
#define NODE_MESH_RESET_POLL_MS 50

/** @brief Checks before a factory reset goes on without the mesh reset; 2 s
 *         in all. */
#define NODE_MESH_RESET_POLLS 40

/** @brief Number of models on the primary element. */
#define NODE_MESH_ROOT_MODEL_COUNT \
  (sizeof(g_root_models) / sizeof(g_root_models[0]))

/** @brief Number of subnets the mesh stack keeps. */
#define NODE_MESH_SUBNET_COUNT (sizeof(bt_mesh.sub) / sizeof(bt_mesh.sub[0]))

/** @brief Number of application keys the mesh stack keeps. */
#define NODE_MESH_APP_KEY_COUNT \
  (sizeof(bt_mesh.app_keys) / sizeof(bt_mesh.app_keys[0]))

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief True once node_mesh_start() was called; written once, from the
 *         boot task. */
static bool g_start_called = false;

/** @brief True once ESP-BLE-MESH is initialised; written once, from the boot
 *         task, and read from other tasks after that. */
static bool g_mesh_ready = false;

/** @brief Models builder of spec 11, or NULL; written from the boot task
 *         before the start. */
static node_mesh_models_builder_t g_builder = NULL;

/** @brief Composition registered this boot; written by node_mesh_start(). */
static node_mesh_comp_plan_t g_plan = {0};

/** @brief Device UUID, "stoneocean" and the Bluetooth MAC; written by
 *         node_mesh_start() before the stack reads it. */
static uint8_t g_uuid[NODE_MESH_COMP_UUID_LEN] = {0};

/** @brief State of the Config Server. Relay is on: the node is mains powered
 *         (spec 10). Written by the mesh stack in Bluedroid's task. */
static esp_ble_mesh_cfg_srv_t g_config_server = {
    .net_transmit = NODE_MESH_TRANSMIT,
    .relay = ESP_BLE_MESH_RELAY_ENABLED,
    .relay_retransmit = NODE_MESH_TRANSMIT,
    .beacon = ESP_BLE_MESH_BEACON_ENABLED,
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_NOT_SUPPORTED,
    .friend_state = ESP_BLE_MESH_FRIEND_NOT_SUPPORTED,
    .default_ttl = NODE_MESH_DEFAULT_TTL,
};

/** @brief Health self-tests the node supports: only the standard test. */
static const uint8_t g_health_test_ids[] = {ESP_BLE_MESH_HEALTH_STANDARD_TEST};

/** @brief State of the Health Server. Written by the mesh stack. */
static esp_ble_mesh_health_srv_t g_health_server = {
    .health_test =
        {
            .id_count = (uint8_t)sizeof(g_health_test_ids),
            .test_ids = g_health_test_ids,
            .company_id = NODE_MESH_CID_ESPRESSIF,
        },
};

/* The macro also defines the publication buffer bt_mesh_pub_msg_g_health_pub,
 * whose name it builds itself. */
/** @brief Publication context of the Health Server. */
ESP_BLE_MESH_HEALTH_PUB_DEFINE(g_health_pub, NODE_MESH_HEALTH_MAX_FAULTS,
                               ROLE_NODE);

/** @brief Models of the primary element (spec 10). */
static esp_ble_mesh_model_t g_root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&g_config_server),
    ESP_BLE_MESH_MODEL_HEALTH_SRV(&g_health_server, &g_health_pub),
};

/** @brief Elements of the composition: the primary one, then one per device
 *         in config order. The first g_plan.element_count are registered.
 *         Filled by node_mesh_start(); the stack writes element_addr. */
static esp_ble_mesh_elem_t g_elements[NODE_MESH_COMP_MAX_ELEMENTS] = {0};

/** @brief Composition data; element_count is set by node_mesh_start(). */
static esp_ble_mesh_comp_t g_comp = {
    .cid = NODE_MESH_CID_ESPRESSIF,
    .elements = g_elements,
};

/** @brief Provisioning setup: no OOB (spec 10). */
static esp_ble_mesh_prov_t g_prov = {
    .uuid = g_uuid,
};

/** @brief True while the factory-reset hook waits for the mesh reset;
 *         written by the hook, read in Bluedroid's task. Atomic because the
 *         two tasks share it. */
static atomic_bool g_factory_reset_pending = false;

/** @brief True once the mesh reset of a factory reset finished; written in
 *         Bluedroid's task, read by the hook. Atomic because the two tasks
 *         share it. */
static atomic_bool g_factory_reset_done = false;

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Read the stored composition hash. A read failure is logged and
 *        counts as no hash stored.
 *
 * @param stored Receives true if a hash is stored.
 * @param hash   Receives the hash; 0 if none is stored.
 */
static void node_mesh_load_hash(bool *stored, uint32_t *hash);

/**
 * @brief Fill g_elements from the plan: the primary element, then one per
 *        device with the models the builder gives.
 *
 * @param cfg  Active config, or NULL in safe mode.
 * @param plan Composition to register.
 */
static void node_mesh_build_elements(const node_config_t *cfg,
                                     const node_mesh_comp_plan_t *plan);

/**
 * @brief Ask the builder for one device's models. A failure is logged and
 *        gives no models.
 *
 * @param dev_index Index of the device in the config.
 * @param dev       The device's config entry.
 * @param models    Receives the model array, or NULL.
 * @param count     Receives the number of models.
 */
static void node_mesh_build_device_models(int dev_index,
                                          const node_config_dev_t *dev,
                                          esp_ble_mesh_model_t **models,
                                          uint8_t *count);

/**
 * @brief Set one entry of g_elements.
 *
 * @param index  Element number; below NODE_MESH_COMP_MAX_ELEMENTS.
 * @param models SIG model array, or NULL if @p count is 0.
 * @param count  Number of models in @p models.
 */
static void node_mesh_set_element(size_t index, esp_ble_mesh_model_t *models,
                                  uint8_t count);

/**
 * @brief Start the Bluetooth controller in BLE-only mode and the Bluedroid
 *        host. Failures are logged.
 *
 * @return NODE_MESH_OK, or NODE_MESH_ERR_BT.
 */
static node_mesh_err_t node_mesh_bt_start(void);

/**
 * @brief Register the provisioning callback and initialise ESP-BLE-MESH
 *        with the composition. Failures are logged.
 *
 * @return NODE_MESH_OK, or NODE_MESH_ERR_MESH.
 */
static node_mesh_err_t node_mesh_stack_init(void);

/**
 * @brief Bring the stored hash in line with the mesh's own provisioning
 *        state: store the hash in use if it is missing, or erase a stale
 *        one.
 *
 * @param provisioned True if the mesh restored a provisioned node.
 * @param hash_stored True if a hash was stored at boot.
 */
static void node_mesh_reconcile_hash(bool provisioned, bool hash_stored);

/**
 * @brief Log the UUID, the mesh state, the keys of a provisioned node and
 *        the composition.
 *
 * @param cfg         Active config, or NULL in safe mode.
 * @param provisioned True if the node is provisioned.
 */
static void node_mesh_report_boot(const node_config_t *cfg, bool provisioned);

/**
 * @brief Log the IV index and the key indexes, read from the mesh stack's
 *        internal state.
 */
static void node_mesh_report_keys(void);

/**
 * @brief Log the free heap and how long the start took.
 *
 * @param start_ms HAL time at which node_mesh_start() was entered, in ms.
 */
static void node_mesh_report_resources(uint32_t start_ms);

/**
 * @brief Whether the relay feature is enabled in the Config Server.
 *
 * @return True if enabled.
 */
static bool node_mesh_relay_enabled(void);

/**
 * @brief Handle a provisioning event of ESP-BLE-MESH. Runs in Bluedroid's
 *        task; only posts to the mesh AO, or flags the end of a factory
 *        reset's mesh reset.
 *
 * @param event The event.
 * @param param Its parameters.
 */
static void node_mesh_prov_cb(esp_ble_mesh_prov_cb_event_t event,
                              esp_ble_mesh_prov_cb_param_t *param);

/**
 * @brief Factory-reset hook (spec 09): reset the mesh node, which erases
 *        its stored mesh state, then erase `meshmeta`. Runs in the task that
 *        factory-resets and waits up to 2 s for the mesh reset.
 */
static void node_mesh_factory_reset_hook(void);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

node_mesh_err_t node_mesh_start(const node_config_t *cfg) {
  if (g_start_called) {
    return NODE_MESH_ERR_STATE;
  }
  const uint32_t start_ms = node_hal_time_ms();

  bool hash_stored = false;
  uint32_t stored_hash = 0;
  node_mesh_load_hash(&hash_stored, &stored_hash);
  node_mesh_comp_plan_t plan = {0};
  node_mesh_err_t err =
      node_mesh_comp_plan(cfg, hash_stored, stored_hash, &plan);
  if (err != NODE_MESH_OK) {
    return err;
  }
  g_start_called = true;
  g_plan = plan;
  if (plan.outcome == NODE_MESH_COMP_MISMATCH) {
    node_mesh_log_mismatch(stored_hash, plan.config_hash);
  }
  node_mesh_build_elements(cfg, &plan);

  uint8_t mac[NODE_MESH_COMP_MAC_LEN] = {0};
  if ((node_mesh_port_bt_mac(mac) != NODE_MESH_PORT_OK) ||
      (node_mesh_comp_uuid(mac, g_uuid) != NODE_MESH_OK)) {
    return NODE_MESH_ERR_BT;
  }
  if (node_health_register_reset_hook(node_mesh_factory_reset_hook) !=
      NODE_HEALTH_OK) {
    node_mesh_log_hook_failed();
  }
  err = node_mesh_ao_start(plan.hash_in_use, plan.element_count);
  if (err != NODE_MESH_OK) {
    return err;
  }
  err = node_mesh_bt_start();
  if (err != NODE_MESH_OK) {
    return err;
  }
  err = node_mesh_stack_init();
  if (err != NODE_MESH_OK) {
    return err;
  }
  g_mesh_ready = true;

  const bool provisioned = esp_ble_mesh_node_is_provisioned();
  node_mesh_reconcile_hash(provisioned, hash_stored);
  if (!provisioned) {
    const esp_err_t esp_err =
        esp_ble_mesh_node_prov_enable(ESP_BLE_MESH_PROV_ADV);
    if (esp_err != ESP_OK) {
      node_mesh_log_call_failed("esp_ble_mesh_node_prov_enable", esp_err);
      return NODE_MESH_ERR_MESH;
    }
  }
  node_mesh_report_boot(cfg, provisioned);
  node_mesh_report_resources(start_ms);
  return NODE_MESH_OK;
}

bool node_mesh_is_provisioned(void) {
  if (!g_mesh_ready) {
    return false;
  }
  return esp_ble_mesh_node_is_provisioned();
}

uint16_t node_mesh_primary_addr(void) {
  if (!node_mesh_is_provisioned()) {
    return ESP_BLE_MESH_ADDR_UNASSIGNED;
  }
  return esp_ble_mesh_get_primary_element_address();
}

esp_ble_mesh_elem_t *node_mesh_device_element(int dev_index) {
  if (!g_mesh_ready || (dev_index < 0) ||
      (dev_index >= (int)g_plan.device_count)) {
    return NULL;
  }
  return &g_elements[1 + dev_index];
}

node_mesh_err_t node_mesh_set_models_builder(
    node_mesh_models_builder_t builder) {
  if (builder == NULL) {
    return NODE_MESH_ERR_ARG;
  }
  if (g_start_called) {
    return NODE_MESH_ERR_STATE;
  }
  g_builder = builder;
  return NODE_MESH_OK;
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static void node_mesh_load_hash(bool *stored, uint32_t *hash) {
  *stored = false;
  *hash = 0;
  const node_mesh_port_err_t err = node_mesh_port_hash_load(hash);
  if (err == NODE_MESH_PORT_OK) {
    *stored = true;
    return;
  }
  *hash = 0;
  if (err != NODE_MESH_PORT_NOT_FOUND) {
    node_mesh_log_hash_load_failed();
  }
}

static void node_mesh_build_elements(const node_config_t *cfg,
                                     const node_mesh_comp_plan_t *plan) {
  node_mesh_set_element(0, g_root_models, (uint8_t)NODE_MESH_ROOT_MODEL_COUNT);
  for (size_t i = 0; (i < plan->device_count) && (cfg != NULL) &&
                     ((i + 1u) < NODE_MESH_COMP_MAX_ELEMENTS);
       i++) {
    esp_ble_mesh_model_t *models = NULL;
    uint8_t count = 0;
    node_mesh_build_device_models((int)i, &cfg->dev[i], &models, &count);
    node_mesh_set_element(i + 1u, models, count);
  }
  g_comp.element_count = plan->element_count;
}

static void node_mesh_build_device_models(int dev_index,
                                          const node_config_dev_t *dev,
                                          esp_ble_mesh_model_t **models,
                                          uint8_t *count) {
  *models = NULL;
  *count = 0;
  if (g_builder == NULL) {
    return;
  }
  esp_ble_mesh_model_t *built = NULL;
  uint8_t built_count = 0;
  const node_mesh_err_t err = g_builder(dev_index, dev, &built, &built_count);
  if (err != NODE_MESH_OK) {
    node_mesh_log_builder_failed(dev_index, err);
    return;
  }
  if ((built == NULL) && (built_count > 0u)) {
    node_mesh_log_builder_failed(dev_index, NODE_MESH_ERR_ARG);
    return;
  }
  *models = built;
  *count = built_count;
}

static void node_mesh_set_element(size_t index, esp_ble_mesh_model_t *models,
                                  uint8_t count) {
  if (index >= NODE_MESH_COMP_MAX_ELEMENTS) {
    return;
  }
  /* esp_ble_mesh_elem_t has const members, so it cannot be assigned. The
   * composition is only known at run time, so each element is built as an
   * initialised local and copied into place before the stack reads it. */
  const esp_ble_mesh_elem_t element = {
      .location = 0,
      .sig_model_count = count,
      .vnd_model_count = 0,
      .sig_models = models,
      .vnd_models = NULL,
  };
  (void)memcpy(&g_elements[index], &element, sizeof(element));
}

static node_mesh_err_t node_mesh_bt_start(void) {
  esp_err_t err = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
  if (err != ESP_OK) {
    node_mesh_log_call_failed("esp_bt_controller_mem_release", err);
    return NODE_MESH_ERR_BT;
  }
  esp_bt_controller_config_t controller_cfg =
      BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  err = esp_bt_controller_init(&controller_cfg);
  if (err != ESP_OK) {
    node_mesh_log_call_failed("esp_bt_controller_init", err);
    return NODE_MESH_ERR_BT;
  }
  err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
  if (err != ESP_OK) {
    node_mesh_log_call_failed("esp_bt_controller_enable", err);
    return NODE_MESH_ERR_BT;
  }
  esp_bluedroid_config_t bluedroid_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
  err = esp_bluedroid_init_with_cfg(&bluedroid_cfg);
  if (err != ESP_OK) {
    node_mesh_log_call_failed("esp_bluedroid_init_with_cfg", err);
    return NODE_MESH_ERR_BT;
  }
  err = esp_bluedroid_enable();
  if (err != ESP_OK) {
    node_mesh_log_call_failed("esp_bluedroid_enable", err);
    return NODE_MESH_ERR_BT;
  }
  return NODE_MESH_OK;
}

static node_mesh_err_t node_mesh_stack_init(void) {
  esp_err_t err = esp_ble_mesh_register_prov_callback(node_mesh_prov_cb);
  if (err != ESP_OK) {
    node_mesh_log_call_failed("esp_ble_mesh_register_prov_callback", err);
    return NODE_MESH_ERR_MESH;
  }
  err = esp_ble_mesh_init(&g_prov, &g_comp);
  if (err != ESP_OK) {
    node_mesh_log_call_failed("esp_ble_mesh_init", err);
    return NODE_MESH_ERR_MESH;
  }
  return NODE_MESH_OK;
}

/* Runs before provisioning is enabled, so the mesh AO cannot be storing a
 * hash at the same time; it is the only write to meshmeta outside the AO. */
static void node_mesh_reconcile_hash(bool provisioned, bool hash_stored) {
  if (provisioned && !hash_stored) {
    node_mesh_log_hash_missing();
    if (node_mesh_port_hash_store(g_plan.hash_in_use) != NODE_MESH_PORT_OK) {
      node_mesh_log_hash_store_failed();
    }
    return;
  }
  if (!provisioned && hash_stored) {
    node_mesh_log_hash_stale();
    if (node_mesh_port_meta_erase() != NODE_MESH_PORT_OK) {
      node_mesh_log_meta_erase_failed();
    }
  }
}

static void node_mesh_report_boot(const node_config_t *cfg, bool provisioned) {
  node_mesh_log_uuid(g_uuid, sizeof(g_uuid));
  node_mesh_log_state(provisioned, node_mesh_primary_addr(),
                      g_plan.element_count, node_mesh_relay_enabled());
  if (provisioned) {
    node_mesh_report_keys();
  }
  node_mesh_log_primary_element(g_elements[0].sig_model_count);
  for (size_t i = 0; (i < g_plan.device_count) && (cfg != NULL) &&
                     ((i + 1u) < NODE_MESH_COMP_MAX_ELEMENTS);
       i++) {
    node_mesh_log_device_element((uint8_t)(i + 1u), &cfg->dev[i],
                                 g_elements[i + 1u].sig_model_count);
  }
}

static void node_mesh_report_keys(void) {
  uint16_t net_idx[NODE_MESH_SUBNET_COUNT] = {0};
  size_t n_net = 0;
  for (size_t i = 0; i < NODE_MESH_SUBNET_COUNT; i++) {
    if (bt_mesh.sub[i].net_idx != BLE_MESH_KEY_UNUSED) {
      net_idx[n_net] = bt_mesh.sub[i].net_idx;
      n_net++;
    }
  }
  uint16_t app_idx[NODE_MESH_APP_KEY_COUNT] = {0};
  size_t n_app = 0;
  for (size_t i = 0; i < NODE_MESH_APP_KEY_COUNT; i++) {
    if (bt_mesh.app_keys[i].net_idx != BLE_MESH_KEY_UNUSED) {
      app_idx[n_app] = bt_mesh.app_keys[i].app_idx;
      n_app++;
    }
  }
  node_mesh_log_keys(bt_mesh.iv_index, net_idx, n_net, app_idx, n_app);
}

static void node_mesh_report_resources(uint32_t start_ms) {
  size_t internal = 0;
  size_t total = 0;
  if (node_mesh_port_free_heap(&internal, &total) == NODE_MESH_PORT_OK) {
    node_mesh_log_heap(internal, total,
                       internal < NODE_MESH_MIN_FREE_INTERNAL_BYTES);
  }
  node_mesh_log_started(node_hal_time_ms() - start_ms);
}

static bool node_mesh_relay_enabled(void) {
  return g_config_server.relay == ESP_BLE_MESH_RELAY_ENABLED;
}

static void node_mesh_prov_cb(esp_ble_mesh_prov_cb_event_t event,
                              esp_ble_mesh_prov_cb_param_t *param) {
  if (param == NULL) {
    return;
  }
  /* A failed post is logged by the mesh AO's post functions; there is
   * nothing more a callback can do about it. */
  switch (event) {
    case ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT:
      if (param->node_prov_enable_comp.err_code != 0) {
        (void)node_mesh_ao_post_notice(NODE_MESH_AO_NOTICE_PROV_ENABLE_FAILED,
                                       param->node_prov_enable_comp.err_code);
      }
      break;
    case ESP_BLE_MESH_NODE_PROV_LINK_OPEN_EVT:
      (void)node_mesh_ao_post_notice(NODE_MESH_AO_NOTICE_LINK_OPEN, 0);
      break;
    case ESP_BLE_MESH_NODE_PROV_LINK_CLOSE_EVT:
      (void)node_mesh_ao_post_notice(NODE_MESH_AO_NOTICE_LINK_CLOSE, 0);
      break;
    case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT:
      (void)node_mesh_ao_post_prov_complete(
          param->node_prov_complete.net_idx, param->node_prov_complete.addr,
          param->node_prov_complete.iv_index, node_mesh_relay_enabled());
      break;
    case ESP_BLE_MESH_NODE_PROV_RESET_EVT:
      /* Also raised by our own esp_ble_mesh_node_local_reset(). During a
       * factory reset the hook restarts the node, not the mesh AO. */
      if (atomic_load(&g_factory_reset_pending)) {
        atomic_store(&g_factory_reset_done, true);
      } else {
        (void)node_mesh_ao_post_node_reset();
      }
      break;
    default:
      break;
  }
}

static void node_mesh_factory_reset_hook(void) {
  if (g_mesh_ready && esp_ble_mesh_node_is_provisioned()) {
    atomic_store(&g_factory_reset_pending, true);
    /* The reset runs later, in Bluedroid's task, and erases the stored mesh
     * state there; the node must not restart before it has finished. */
    const esp_err_t err = esp_ble_mesh_node_local_reset();
    if (err != ESP_OK) {
      node_mesh_log_call_failed("esp_ble_mesh_node_local_reset", err);
    } else {
      for (uint32_t i = 0;
           (i < NODE_MESH_RESET_POLLS) && !atomic_load(&g_factory_reset_done);
           i++) {
        node_hal_time_delay_ms(NODE_MESH_RESET_POLL_MS);
      }
      if (!atomic_load(&g_factory_reset_done)) {
        node_mesh_log_reset_timeout();
      }
    }
  }
  if (node_mesh_port_meta_erase() != NODE_MESH_PORT_OK) {
    node_mesh_log_meta_erase_failed();
  }
}
