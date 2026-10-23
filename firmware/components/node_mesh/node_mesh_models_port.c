/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include "node_mesh_models_port.h"

#include <stddef.h>
#include <stdint.h>

#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_err.h"
#include "node_mesh.h"
#include "node_mesh_err.h"
#include "node_mesh_internal.h"
#include "node_mesh_log.h"
#include "node_mesh_msg.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Company ID of the Stone Ocean vendor models (spec 11).
 *         TODO(confirm): Espressif's ID until Stone Ocean has its own
 *         Bluetooth SIG company ID. */
#define NODE_MESH_MODELS_CID 0x02E5

/** @brief Model ID of the Stone Ocean Node Server (spec 11). The gateway's
 *         Node Client is 0x0002. */
#define NODE_MESH_MODELS_SERVER_ID 0x0001

/** @brief 3-octet vendor opcode of an opcode number NODE_MESH_MSG_OP_*. */
#define NODE_MESH_MODELS_OPCODE(op) \
  ESP_BLE_MESH_MODEL_OP_3((op), NODE_MESH_MODELS_CID)

/** @brief Bits of a 3-octet vendor opcode that hold the opcode number. */
#define NODE_MESH_MODELS_OP_MASK 0x3Fu

/** @brief Position of the opcode number in a 3-octet vendor opcode. */
#define NODE_MESH_MODELS_OP_SHIFT 16u

/** @brief Length of a 3-octet vendor opcode, in bytes. */
#define NODE_MESH_MODELS_OPCODE_LEN 3

/** @brief Size of the publication buffer: the opcode and the largest
 *         published payload, a DeviceStatus. */
#define NODE_MESH_MODELS_PUB_MSG_LEN \
  (NODE_MESH_MODELS_OPCODE_LEN + NODE_MESH_MSG_MAX_UNSEGMENTED)

/** @brief Number of vendor models on the primary element. */
#define NODE_MESH_MODELS_VENDOR_COUNT \
  (sizeof(g_vendor_models) / sizeof(g_vendor_models[0]))

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief Requests the Node Server accepts. ESP-BLE-MESH drops any other
 *         opcode before the callback. Payloads may be empty, so the minimum
 *         length is 0. */
static esp_ble_mesh_model_op_t g_server_ops[] = {
    ESP_BLE_MESH_MODEL_OP(
        NODE_MESH_MODELS_OPCODE(NODE_MESH_MSG_OP_DEVICE_LIST_GET), 0),
    ESP_BLE_MESH_MODEL_OP(NODE_MESH_MODELS_OPCODE(NODE_MESH_MSG_OP_DEVICE_GET),
                          0),
    ESP_BLE_MESH_MODEL_OP(NODE_MESH_MODELS_OPCODE(NODE_MESH_MSG_OP_ONOFF_SET),
                          0),
    ESP_BLE_MESH_MODEL_OP(
        NODE_MESH_MODELS_OPCODE(NODE_MESH_MSG_OP_ONOFF_SET_UNACK), 0),
    ESP_BLE_MESH_MODEL_OP_END,
};

/* The macro also defines the publication buffer bt_mesh_pub_msg_g_server_pub,
 * whose name it builds itself. */
/** @brief Publication context of the Node Server; the gateway sets its
 *         publish address. Written by the mesh stack in Bluedroid's task. */
ESP_BLE_MESH_MODEL_PUB_DEFINE(g_server_pub, NODE_MESH_MODELS_PUB_MSG_LEN,
                              ROLE_NODE);

/** @brief Vendor models of the primary element: the Node Server. */
static esp_ble_mesh_model_t g_vendor_models[] = {
    ESP_BLE_MESH_VENDOR_MODEL(NODE_MESH_MODELS_CID, NODE_MESH_MODELS_SERVER_ID,
                              g_server_ops, &g_server_pub, NULL),
};

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Handle an event of ESP-BLE-MESH's custom models. Runs in Bluedroid's
 *        task; only posts requests to the models AO.
 *
 * @param event The event.
 * @param param Its parameters.
 */
static void node_mesh_models_port_cb(esp_ble_mesh_model_cb_event_t event,
                                     esp_ble_mesh_model_cb_param_t *param);

/**
 * @brief Pass one received message to the models AO, if it is a request to
 *        the Node Server.
 *
 * @param op The message's parameters; must not be NULL.
 */
static void node_mesh_models_port_on_operation(
    const struct ble_mesh_model_operation_evt_param *op);

/**
 * @brief Map an ESP-IDF error of a send or publish to a node_mesh error,
 *        logging it.
 *
 * @param what NUL-terminated name of the call.
 * @param err  ESP-IDF error.
 * @return NODE_MESH_OK, or NODE_MESH_ERR_MESH.
 */
static node_mesh_err_t node_mesh_models_port_result(const char *what,
                                                    esp_err_t err);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

node_mesh_err_t node_mesh_models_port_register(void) {
  const esp_err_t err =
      esp_ble_mesh_register_custom_model_callback(node_mesh_models_port_cb);
  if (err != ESP_OK) {
    node_mesh_log_call_failed("esp_ble_mesh_register_custom_model_callback",
                              err);
    return NODE_MESH_ERR_MESH;
  }
  return node_mesh_set_vendor_models(g_vendor_models,
                                     (uint8_t)NODE_MESH_MODELS_VENDOR_COUNT);
}

node_mesh_err_t node_mesh_models_port_publish(uint8_t op,
                                              const uint8_t *payload,
                                              size_t len) {
  if (((payload == NULL) && (len > 0u)) ||
      (len > NODE_MESH_MSG_MAX_UNSEGMENTED)) {
    return NODE_MESH_ERR_ARG;
  }
  /* The stack writes publish_addr in Bluedroid's task when the gateway sets
   * the publication. A 16-bit read here can only see the old or the new
   * address, and either is a valid answer at this moment. */
  if (!node_mesh_is_provisioned() ||
      (g_server_pub.publish_addr == ESP_BLE_MESH_ADDR_UNASSIGNED)) {
    return NODE_MESH_ERR_STATE;
  }
  /* ESP-BLE-MESH copies the payload before this returns, and never writes
   * it, despite the non-const parameter. */
  const esp_err_t err = esp_ble_mesh_model_publish(
      &g_vendor_models[0], NODE_MESH_MODELS_OPCODE(op), (uint16_t)len,
      (uint8_t *)payload, ROLE_NODE);
  return node_mesh_models_port_result("esp_ble_mesh_model_publish", err);
}

node_mesh_err_t node_mesh_models_port_send(
    const node_mesh_models_port_ctx_t *ctx, uint8_t op, const uint8_t *payload,
    size_t len) {
  if ((ctx == NULL) || ((payload == NULL) && (len > 0u)) ||
      (len > NODE_MESH_MSG_DEVICE_LIST_MAX_LEN)) {
    return NODE_MESH_ERR_ARG;
  }
  esp_ble_mesh_msg_ctx_t mesh_ctx = {
      .net_idx = ctx->net_idx,
      .app_idx = ctx->app_idx,
      .addr = ctx->addr,
      .send_ttl = ESP_BLE_MESH_TTL_DEFAULT,
  };
  /* As for publish: the payload is copied and never written. */
  const esp_err_t err = esp_ble_mesh_server_model_send_msg(
      &g_vendor_models[0], &mesh_ctx, NODE_MESH_MODELS_OPCODE(op),
      (uint16_t)len, (uint8_t *)payload);
  return node_mesh_models_port_result("esp_ble_mesh_server_model_send_msg",
                                      err);
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static void node_mesh_models_port_cb(esp_ble_mesh_model_cb_event_t event,
                                     esp_ble_mesh_model_cb_param_t *param) {
  if (param == NULL) {
    return;
  }
  switch (event) {
    case ESP_BLE_MESH_MODEL_OPERATION_EVT:
      node_mesh_models_port_on_operation(&param->model_operation);
      break;
    default:
      /* Send and publish completions are not tracked in v1: a message the
       * stack accepted is as good as sent, and a refusal is logged when the
       * call returns. */
      break;
  }
}

static void node_mesh_models_port_on_operation(
    const struct ble_mesh_model_operation_evt_param *op) {
  if ((op->model != &g_vendor_models[0]) || (op->ctx == NULL)) {
    return;
  }
  const node_mesh_models_port_ctx_t ctx = {
      .net_idx = op->ctx->net_idx,
      .app_idx = op->ctx->app_idx,
      .addr = op->ctx->addr,
  };
  const uint8_t op_number =
      (uint8_t)((op->opcode >> NODE_MESH_MODELS_OP_SHIFT) &
                NODE_MESH_MODELS_OP_MASK);
  /* A failure to post is logged by the models AO's post function; there is
   * nothing more a callback can do about it. */
  (void)node_mesh_models_post_request(&ctx, op_number, op->msg, op->length);
}

static node_mesh_err_t node_mesh_models_port_result(const char *what,
                                                    esp_err_t err) {
  if (err != ESP_OK) {
    node_mesh_log_call_failed(what, err);
    return NODE_MESH_ERR_MESH;
  }
  return NODE_MESH_OK;
}
