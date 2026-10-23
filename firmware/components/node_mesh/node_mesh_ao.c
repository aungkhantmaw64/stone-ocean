/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "node_ao.h"
#include "node_mesh_err.h"
#include "node_mesh_internal.h"
#include "node_mesh_log.h"
#include "node_mesh_port.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Name of the mesh AO's thread, also used in logs. */
#define NODE_MESH_AO_NAME "node_mesh"

/** @brief FreeRTOS priority of the mesh AO. It handles rare events and only
 *         logs, so it stays well below the Bluetooth tasks (spec 10). */
#define NODE_MESH_AO_PRIORITY 4

/** @brief Stack size of the mesh AO's thread, in bytes. Its deepest path is
 *         a log line, as in the health AO. */
#define NODE_MESH_AO_STACK_BYTES 4096

/** @brief Events the mesh AO's queue holds. Provisioning and node reset each
 *         happen at most once per boot. */
#define NODE_MESH_AO_QUEUE_LEN 4

/* ==========================================================================
 * TYPES
 * ========================================================================== */

/** @brief Private signals of the mesh AO. */
enum {
  NODE_MESH_AO_SIG_PROV_COMPLETE = NODE_AO_SIG_USER, /**< Provisioning
                                                        completed; carries a
                                                        node_mesh_ao_prov_evt_t.
                                                        From Bluedroid's
                                                        task. */
  NODE_MESH_AO_SIG_NODE_RESET, /**< The gateway reset the node; a plain
                                  node_ao_evt_t. From Bluedroid's task. */
  NODE_MESH_AO_SIG_NOTICE,     /**< Provisioning progress to log; carries a
                                  node_mesh_ao_notice_evt_t. From
                                  Bluedroid's task. */
};

/** @brief Event of NODE_MESH_AO_SIG_PROV_COMPLETE. */
typedef struct {
  node_ao_evt_t super; /**< Header; sig is NODE_MESH_AO_SIG_PROV_COMPLETE. */
  uint32_t iv_index;   /**< IV index of the network. */
  uint16_t net_idx;    /**< Index of the network key, 0 to 0xFFF. */
  uint16_t addr;       /**< Unicast address of the primary element. */
  bool relay;          /**< True if the relay feature is enabled. */
} node_mesh_ao_prov_evt_t;

_Static_assert(sizeof(node_mesh_ao_prov_evt_t) <= NODE_AO_MAX_EVT_SIZE,
               "the provisioning event must fit an AO queue slot");

/** @brief Event of NODE_MESH_AO_SIG_NOTICE. */
typedef struct {
  node_ao_evt_t super;          /**< Header; sig is NODE_MESH_AO_SIG_NOTICE. */
  int32_t err;                  /**< ESP-BLE-MESH error code, or 0. */
  node_mesh_ao_notice_t notice; /**< What happened. */
} node_mesh_ao_notice_evt_t;

_Static_assert(sizeof(node_mesh_ao_notice_evt_t) <=
                   sizeof(node_mesh_ao_prov_evt_t),
               "the queue slot is sized for the provisioning event");

/** @brief The mesh active object. */
typedef struct {
  node_ao_t super;       /**< Framework part; must be first. */
  uint8_t element_count; /**< Elements registered this boot; set before the
                            AO starts. */
} node_mesh_ao_t;

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief True once node_mesh_ao_start() started the mesh AO; written once,
 *         from the boot task. */
static bool g_started = false;

/** @brief The mesh AO; after the start its state is touched only in its own
 *         thread. */
static node_mesh_ao_t g_mesh = {0};

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Handle one event of the mesh AO.
 *
 * @param me The mesh AO.
 * @param e  The event.
 */
static void node_mesh_ao_dispatch(node_ao_t *me, const node_ao_evt_t *e);

/**
 * @brief Log the provisioned state.
 *
 * @param me  The mesh AO.
 * @param evt The provisioning event.
 */
static void node_mesh_ao_on_prov_complete(const node_mesh_ao_t *me,
                                          const node_mesh_ao_prov_evt_t *evt);

/**
 * @brief Log the reset and restart the node.
 */
static void node_mesh_ao_on_node_reset(void);

/**
 * @brief Post an event to the mesh AO, logging a failure.
 *
 * @param evt  The event; must not be NULL.
 * @param size Size of @p evt in bytes.
 * @param what NUL-terminated name of the event, for the log.
 * @return NODE_MESH_OK, or NODE_MESH_ERR_STATE if it was not queued.
 */
static node_mesh_err_t node_mesh_ao_post(const void *evt, size_t size,
                                         const char *what);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

node_mesh_err_t node_mesh_ao_start(uint8_t element_count) {
  if (g_started) {
    return NODE_MESH_ERR_STATE;
  }
  g_mesh.element_count = element_count;
  const node_ao_config_t ao_cfg = {
      .name = NODE_MESH_AO_NAME,
      .priority = NODE_MESH_AO_PRIORITY,
      .stack_bytes = NODE_MESH_AO_STACK_BYTES,
      .queue_len = NODE_MESH_AO_QUEUE_LEN,
      .evt_size = sizeof(node_mesh_ao_prov_evt_t),
  };
  if (node_ao_start(&g_mesh.super, node_mesh_ao_dispatch, &ao_cfg) !=
      NODE_AO_OK) {
    return NODE_MESH_ERR_STATE;
  }
  g_started = true;
  return NODE_MESH_OK;
}

node_mesh_err_t node_mesh_ao_post_prov_complete(uint16_t net_idx, uint16_t addr,
                                                uint32_t iv_index, bool relay) {
  const node_mesh_ao_prov_evt_t evt = {
      .super = {.sig = NODE_MESH_AO_SIG_PROV_COMPLETE},
      .iv_index = iv_index,
      .net_idx = net_idx,
      .addr = addr,
      .relay = relay,
  };
  return node_mesh_ao_post(&evt, sizeof(evt), "prov complete");
}

node_mesh_err_t node_mesh_ao_post_node_reset(void) {
  const node_ao_evt_t evt = {.sig = NODE_MESH_AO_SIG_NODE_RESET};
  return node_mesh_ao_post(&evt, sizeof(evt), "node reset");
}

node_mesh_err_t node_mesh_ao_post_notice(node_mesh_ao_notice_t notice,
                                         int32_t err) {
  const node_mesh_ao_notice_evt_t evt = {
      .super = {.sig = NODE_MESH_AO_SIG_NOTICE},
      .err = err,
      .notice = notice,
  };
  return node_mesh_ao_post(&evt, sizeof(evt), "notice");
}

void node_mesh_ao_reset(void) {
  g_started = false;
  (void)memset(&g_mesh, 0, sizeof(g_mesh));
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static void node_mesh_ao_dispatch(node_ao_t *me, const node_ao_evt_t *e) {
  if ((me == NULL) || (e == NULL)) {
    return;
  }
  const node_mesh_ao_t *mesh = (const node_mesh_ao_t *)me;
  switch (e->sig) {
    case NODE_MESH_AO_SIG_PROV_COMPLETE:
      node_mesh_ao_on_prov_complete(mesh, (const node_mesh_ao_prov_evt_t *)e);
      break;
    case NODE_MESH_AO_SIG_NODE_RESET:
      node_mesh_ao_on_node_reset();
      break;
    case NODE_MESH_AO_SIG_NOTICE: {
      const node_mesh_ao_notice_evt_t *notice =
          (const node_mesh_ao_notice_evt_t *)e;
      node_mesh_log_notice(notice->notice, notice->err);
      break;
    }
    default:
      break;
  }
}

static void node_mesh_ao_on_prov_complete(const node_mesh_ao_t *me,
                                          const node_mesh_ao_prov_evt_t *evt) {
  node_mesh_log_prov_complete(evt->addr, me->element_count, evt->relay,
                              evt->net_idx, evt->iv_index);
}

static void node_mesh_ao_on_node_reset(void) {
  node_mesh_log_node_reset();
  node_mesh_port_restart();
}

static node_mesh_err_t node_mesh_ao_post(const void *evt, size_t size,
                                         const char *what) {
  if (!g_started || (node_ao_post(&g_mesh.super, evt, size) != NODE_AO_OK)) {
    node_mesh_log_post_failed(what);
    return NODE_MESH_ERR_STATE;
  }
  return NODE_MESH_OK;
}
