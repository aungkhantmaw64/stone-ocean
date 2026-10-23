/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include "node_mesh_models.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "node_ao.h"
#include "node_config.h"
#include "node_dev.h"
#include "node_mesh_err.h"
#include "node_mesh_internal.h"
#include "node_mesh_log.h"
#include "node_mesh_models_port.h"
#include "node_mesh_msg.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Name of the models AO's thread, also used in logs. */
#define NODE_MESH_MODELS_AO_NAME "node_mesh_models"

/** @brief FreeRTOS priority of the models AO: below the devices AO (5), whose
 *         events it consumes, and below the Bluetooth tasks (spec 11). */
#define NODE_MESH_MODELS_AO_PRIORITY 4

/** @brief Stack size of the models AO's thread, in bytes. Its deepest path
 *         encodes a DeviceList (a nanopb struct and a 168-byte buffer). */
#define NODE_MESH_MODELS_AO_STACK_BYTES 4096

/** @brief Events the models AO's queue holds: one event from each of the
 *         NODE_CONFIG_MAX_DEVICES devices at boot, plus mesh requests. */
#define NODE_MESH_MODELS_AO_QUEUE_LEN 16

/* ==========================================================================
 * TYPES
 * ========================================================================== */

/** @brief What the models AO knows about one device, from its published
 *         events. */
typedef struct {
  node_dev_value_t value; /**< Latest value; valid while has_value. */
  bool has_value;         /**< True once a value arrived since the device
                             last entered FAULT. */
  bool fault;             /**< True while the device is in FAULT. */
} node_mesh_models_dev_t;

/** @brief The models active object. */
typedef struct {
  node_ao_t super;          /**< Framework part; must be first. */
  const node_config_t *cfg; /**< Active config, or NULL in safe mode;
                               set before the AO starts. */
  int n_dev;                /**< Devices in cfg, 0 to
                               NODE_CONFIG_MAX_DEVICES; set before the
                               AO starts. */
  node_mesh_models_dev_t
      devs[NODE_CONFIG_MAX_DEVICES]; /**< State of each device, by index;
                                        written only in the AO's thread. */
} node_mesh_models_ao_t;

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief Signals of the devices AO the models AO subscribes to. */
static const node_ao_sig_t g_dev_sigs[] = {
    NODE_DEV_SIG_READING,
    NODE_DEV_SIG_MOTION,
    NODE_DEV_SIG_ONOFF_CHANGED,
    NODE_DEV_SIG_FAULT,
};

/** @brief True once node_mesh_models_start() started the models AO; written
 *         once, from the boot task. */
static bool g_started = false;

/** @brief The models AO; after the start its state is touched only in its
 *         own thread. */
static node_mesh_models_ao_t g_models = {0};

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Handle one event of the models AO.
 *
 * @param me The models AO.
 * @param e  The event.
 */
static void node_mesh_models_dispatch(node_ao_t *me, const node_ao_evt_t *e);

/**
 * @brief Record a device event and publish the device's new status.
 *
 * @param me  The models AO.
 * @param evt The devices AO's event.
 */
static void node_mesh_models_on_dev_evt(node_mesh_models_ao_t *me,
                                        const node_dev_evt_t *evt);

/**
 * @brief Publish one device's DeviceStatus, if the gateway set a publish
 *        address. A failure is logged.
 *
 * @param me        The models AO.
 * @param dev_index Device index, below me->n_dev.
 */
static void node_mesh_models_publish_status(const node_mesh_models_ao_t *me,
                                            int dev_index);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

node_mesh_err_t node_mesh_models_start(const node_config_t *cfg) {
  if (g_started) {
    return NODE_MESH_ERR_STATE;
  }
  if ((cfg != NULL) &&
      ((cfg->n_dev < 0) || (cfg->n_dev > NODE_CONFIG_MAX_DEVICES))) {
    return NODE_MESH_ERR_ARG;
  }
  g_models.cfg = cfg;
  g_models.n_dev = (cfg != NULL) ? cfg->n_dev : 0;
  for (size_t i = 0; i < (sizeof(g_dev_sigs) / sizeof(g_dev_sigs[0])); i++) {
    if (node_ao_pubsub_subscribe(&g_models.super, g_dev_sigs[i]) !=
        NODE_AO_OK) {
      return NODE_MESH_ERR_STATE;
    }
  }
  const node_ao_config_t ao_cfg = {
      .name = NODE_MESH_MODELS_AO_NAME,
      .priority = NODE_MESH_MODELS_AO_PRIORITY,
      .stack_bytes = NODE_MESH_MODELS_AO_STACK_BYTES,
      .queue_len = NODE_MESH_MODELS_AO_QUEUE_LEN,
      .evt_size = NODE_AO_MAX_EVT_SIZE,
  };
  if (node_ao_start(&g_models.super, node_mesh_models_dispatch, &ao_cfg) !=
      NODE_AO_OK) {
    return NODE_MESH_ERR_STATE;
  }
  g_started = true;
  if (node_mesh_models_port_register() != NODE_MESH_OK) {
    return NODE_MESH_ERR_STATE;
  }
  return NODE_MESH_OK;
}

void node_mesh_models_reset(void) {
  g_started = false;
  (void)memset(&g_models, 0, sizeof(g_models));
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static void node_mesh_models_dispatch(node_ao_t *me, const node_ao_evt_t *e) {
  if ((me == NULL) || (e == NULL)) {
    return;
  }
  node_mesh_models_ao_t *models = (node_mesh_models_ao_t *)me;
  switch (e->sig) {
    case NODE_DEV_SIG_READING:
    case NODE_DEV_SIG_MOTION:
    case NODE_DEV_SIG_ONOFF_CHANGED:
    case NODE_DEV_SIG_FAULT:
      node_mesh_models_on_dev_evt(models, (const node_dev_evt_t *)e);
      break;
    default:
      break;
  }
}

static void node_mesh_models_on_dev_evt(node_mesh_models_ao_t *me,
                                        const node_dev_evt_t *evt) {
  const int dev_index = (int)evt->dev_index;
  if (dev_index >= me->n_dev) {
    node_mesh_log_models_unknown_device(dev_index);
    return;
  }
  node_mesh_models_dev_t *dev = &me->devs[dev_index];
  if (evt->super.sig == NODE_DEV_SIG_FAULT) {
    dev->fault = evt->fault;
    if (evt->fault) {
      /* The last value is stale from now on: it must not be reported after
       * the device recovers, before its next value arrives. */
      dev->has_value = false;
    }
    node_mesh_log_models_fault(dev_index, evt->fault);
  } else {
    dev->value = evt->value;
    dev->has_value = true;
  }
  node_mesh_models_publish_status(me, dev_index);
}

static void node_mesh_models_publish_status(const node_mesh_models_ao_t *me,
                                            int dev_index) {
  const node_mesh_models_dev_t *dev = &me->devs[dev_index];
  uint8_t payload[NODE_MESH_MSG_MAX_UNSEGMENTED] = {0};
  size_t len = 0;
  node_mesh_err_t err = node_mesh_msg_encode_device_status(
      dev_index, me->cfg->dev[dev_index].type,
      dev->has_value ? &dev->value : NULL, dev->fault, payload, sizeof(payload),
      &len);
  if (err == NODE_MESH_OK) {
    err = node_mesh_models_port_publish(NODE_MESH_MSG_OP_DEVICE_STATUS, payload,
                                        len);
  }
  /* NODE_MESH_ERR_STATE means no publish address is set, or the node is not
   * provisioned: nothing to publish, and nothing to report. */
  if ((err != NODE_MESH_OK) && (err != NODE_MESH_ERR_STATE)) {
    node_mesh_log_models_publish_failed(dev_index, err);
  }
}
