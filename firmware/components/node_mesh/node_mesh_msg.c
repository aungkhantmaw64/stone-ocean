/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include "node_mesh_msg.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "node_config.h"
#include "node_dev.h"
#include "node_mesh_err.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include "stone_ocean/mesh/v1/node.pb.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Temperature and humidity are sent in tenths of their unit. */
#define NODE_MESH_MSG_TENTHS_PER_UNIT 10.0f

/** @brief Lowest relative humidity sent, in percent. */
#define NODE_MESH_MSG_RH_MIN_PCT 0.0f

/** @brief Highest relative humidity sent, in percent. */
#define NODE_MESH_MSG_RH_MAX_PCT 100.0f

_Static_assert(NODE_MESH_MSG_DEVICE_LIST_MAX_LEN ==
                   stoneocean_mesh_v1_DeviceList_size,
               "the DeviceList bound must follow node.options");

_Static_assert(pb_arraysize(stoneocean_mesh_v1_DeviceList, devices) ==
                   NODE_CONFIG_MAX_DEVICES,
               "DeviceList must hold every configured device");

_Static_assert(sizeof(((stoneocean_mesh_v1_DeviceInfo *)NULL)->id) ==
                   NODE_CONFIG_ID_LEN,
               "a DeviceInfo id must hold every config id");

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief Stand-in for the NULL payload of an empty request, which nanopb
 *         cannot read from. Never written. */
static const uint8_t g_empty_payload[1] = {0};

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Protobuf device type of a config device type.
 *
 * @param type Config device type.
 * @param out  Receives the protobuf type; must not be NULL.
 * @return True, or false for a type out of range.
 */
static bool node_mesh_msg_device_type(node_config_dev_type_t type,
                                      stoneocean_mesh_v1_DeviceType *out);

/**
 * @brief Fill the value fields of a DeviceStatus for one device type.
 *
 * @param type  Device type; in range.
 * @param value The device's latest value; must not be NULL.
 * @param msg   Message to fill; must not be NULL.
 */
static void node_mesh_msg_set_values(node_config_dev_type_t type,
                                     const node_dev_value_t *value,
                                     stoneocean_mesh_v1_DeviceStatus *msg);

/**
 * @brief Clamp a value to a range and convert it to tenths, rounding half
 *        away from zero.
 *
 * @param value Value in its unit; finite.
 * @param min   Lowest value kept.
 * @param max   Highest value kept.
 * @return The clamped value in tenths.
 */
static int32_t node_mesh_msg_to_tenths(float value, float min, float max);

/**
 * @brief Encode a message into a buffer.
 *
 * @param fields nanopb descriptor of the message.
 * @param msg    The message.
 * @param out    Receives the payload; must not be NULL.
 * @param cap    Size of @p out in bytes.
 * @param len    Receives the payload length; 0 on failure. Must not be NULL.
 * @return NODE_MESH_OK, or NODE_MESH_ERR_ARG if the buffer is too small.
 */
static node_mesh_err_t node_mesh_msg_encode(const pb_msgdesc_t *fields,
                                            const void *msg, uint8_t *out,
                                            size_t cap, size_t *len);

/**
 * @brief Decode a request payload into a message.
 *
 * @param fields nanopb descriptor of the message.
 * @param in     Payload; may be NULL if @p len is 0.
 * @param len    Payload length in bytes.
 * @param msg    Receives the message; must not be NULL.
 * @return NODE_MESH_OK, or NODE_MESH_ERR_ARG for a NULL payload with a
 *         length, a payload above NODE_MESH_MSG_MAX_UNSEGMENTED bytes, or
 *         one that does not decode.
 */
static node_mesh_err_t node_mesh_msg_decode(const pb_msgdesc_t *fields,
                                            const uint8_t *in, size_t len,
                                            void *msg);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

node_mesh_err_t node_mesh_msg_encode_device_status(
    int dev_index, node_config_dev_type_t type, const node_dev_value_t *value,
    bool fault, uint8_t *out, size_t cap, size_t *len) {
  if (len == NULL) {
    return NODE_MESH_ERR_ARG;
  }
  *len = 0;
  stoneocean_mesh_v1_DeviceType pb_type =
      stoneocean_mesh_v1_DeviceType_DEVICE_TYPE_UNSPECIFIED;
  if ((out == NULL) || (dev_index < 0) ||
      (dev_index >= NODE_CONFIG_MAX_DEVICES) ||
      !node_mesh_msg_device_type(type, &pb_type)) {
    return NODE_MESH_ERR_ARG;
  }
  stoneocean_mesh_v1_DeviceStatus msg =
      stoneocean_mesh_v1_DeviceStatus_init_zero;
  msg.device = (uint32_t)dev_index;
  msg.fault = fault;
  if (!fault && (value != NULL)) {
    node_mesh_msg_set_values(type, value, &msg);
  }
  return node_mesh_msg_encode(stoneocean_mesh_v1_DeviceStatus_fields, &msg, out,
                              cap, len);
}

node_mesh_err_t node_mesh_msg_encode_device_list(const node_config_t *cfg,
                                                 uint8_t *out, size_t cap,
                                                 size_t *len) {
  if (len == NULL) {
    return NODE_MESH_ERR_ARG;
  }
  *len = 0;
  if (out == NULL) {
    return NODE_MESH_ERR_ARG;
  }
  stoneocean_mesh_v1_DeviceList msg = stoneocean_mesh_v1_DeviceList_init_zero;
  if (cfg != NULL) {
    if ((cfg->n_dev < 0) || (cfg->n_dev > NODE_CONFIG_MAX_DEVICES)) {
      return NODE_MESH_ERR_ARG;
    }
    for (int i = 0; i < cfg->n_dev; i++) {
      stoneocean_mesh_v1_DeviceInfo *info = &msg.devices[i];
      if (!node_mesh_msg_device_type(cfg->dev[i].type, &info->type)) {
        return NODE_MESH_ERR_ARG;
      }
      (void)memcpy(info->id, cfg->dev[i].id, sizeof(info->id));
      info->id[sizeof(info->id) - 1u] = '\0';
    }
    msg.devices_count = (pb_size_t)cfg->n_dev;
  }
  return node_mesh_msg_encode(stoneocean_mesh_v1_DeviceList_fields, &msg, out,
                              cap, len);
}

node_mesh_err_t node_mesh_msg_decode_device_get(const uint8_t *in, size_t len,
                                                bool *all, int *dev_index) {
  if ((all == NULL) || (dev_index == NULL)) {
    return NODE_MESH_ERR_ARG;
  }
  *all = false;
  *dev_index = 0;
  stoneocean_mesh_v1_DeviceGet msg = stoneocean_mesh_v1_DeviceGet_init_zero;
  const node_mesh_err_t err =
      node_mesh_msg_decode(stoneocean_mesh_v1_DeviceGet_fields, in, len, &msg);
  if (err != NODE_MESH_OK) {
    return err;
  }
  if (!msg.has_device) {
    *all = true;
    return NODE_MESH_OK;
  }
  if (msg.device >= (uint32_t)NODE_CONFIG_MAX_DEVICES) {
    return NODE_MESH_ERR_ARG;
  }
  *dev_index = (int)msg.device;
  return NODE_MESH_OK;
}

node_mesh_err_t node_mesh_msg_decode_onoff_set(const uint8_t *in, size_t len,
                                               int *dev_index, bool *on) {
  if ((dev_index == NULL) || (on == NULL)) {
    return NODE_MESH_ERR_ARG;
  }
  *dev_index = 0;
  *on = false;
  stoneocean_mesh_v1_OnOffSet msg = stoneocean_mesh_v1_OnOffSet_init_zero;
  const node_mesh_err_t err =
      node_mesh_msg_decode(stoneocean_mesh_v1_OnOffSet_fields, in, len, &msg);
  if (err != NODE_MESH_OK) {
    return err;
  }
  if (msg.device >= (uint32_t)NODE_CONFIG_MAX_DEVICES) {
    return NODE_MESH_ERR_ARG;
  }
  *dev_index = (int)msg.device;
  *on = msg.on;
  return NODE_MESH_OK;
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static bool node_mesh_msg_device_type(node_config_dev_type_t type,
                                      stoneocean_mesh_v1_DeviceType *out) {
  switch (type) {
    case NODE_CONFIG_DEV_SHTC3:
      *out = stoneocean_mesh_v1_DeviceType_DEVICE_TYPE_SHTC3;
      return true;
    case NODE_CONFIG_DEV_PIR:
      *out = stoneocean_mesh_v1_DeviceType_DEVICE_TYPE_PIR;
      return true;
    case NODE_CONFIG_DEV_LED:
      *out = stoneocean_mesh_v1_DeviceType_DEVICE_TYPE_LED;
      return true;
    default:
      return false;
  }
}

static void node_mesh_msg_set_values(node_config_dev_type_t type,
                                     const node_dev_value_t *value,
                                     stoneocean_mesh_v1_DeviceStatus *msg) {
  switch (type) {
    case NODE_CONFIG_DEV_SHTC3:
      if (isfinite(value->temp_c)) {
        msg->has_temperature_dc = true;
        msg->temperature_dc = node_mesh_msg_to_tenths(
            value->temp_c, NODE_MESH_MSG_TEMP_MIN_C, NODE_MESH_MSG_TEMP_MAX_C);
      }
      if (isfinite(value->rh_pct)) {
        msg->has_humidity_dpct = true;
        msg->humidity_dpct = (uint32_t)node_mesh_msg_to_tenths(
            value->rh_pct, NODE_MESH_MSG_RH_MIN_PCT, NODE_MESH_MSG_RH_MAX_PCT);
      }
      break;
    case NODE_CONFIG_DEV_PIR:
      msg->has_motion = true;
      msg->motion = value->motion;
      break;
    case NODE_CONFIG_DEV_LED:
      msg->has_on = true;
      msg->on = value->on;
      break;
    default:
      break;
  }
}

static int32_t node_mesh_msg_to_tenths(float value, float min, float max) {
  float clamped = value;
  if (clamped < min) {
    clamped = min;
  } else if (clamped > max) {
    clamped = max;
  } else {
    /* In range: kept as is. */
  }
  return (int32_t)lroundf(clamped * NODE_MESH_MSG_TENTHS_PER_UNIT);
}

static node_mesh_err_t node_mesh_msg_encode(const pb_msgdesc_t *fields,
                                            const void *msg, uint8_t *out,
                                            size_t cap, size_t *len) {
  pb_ostream_t stream = pb_ostream_from_buffer(out, cap);
  if (!pb_encode(&stream, fields, msg)) {
    return NODE_MESH_ERR_ARG;
  }
  *len = stream.bytes_written;
  return NODE_MESH_OK;
}

static node_mesh_err_t node_mesh_msg_decode(const pb_msgdesc_t *fields,
                                            const uint8_t *in, size_t len,
                                            void *msg) {
  if (((in == NULL) && (len > 0u)) || (len > NODE_MESH_MSG_MAX_UNSEGMENTED)) {
    return NODE_MESH_ERR_ARG;
  }
  pb_istream_t stream =
      pb_istream_from_buffer((in != NULL) ? in : g_empty_payload, len);
  if (!pb_decode(&stream, fields, msg)) {
    return NODE_MESH_ERR_ARG;
  }
  return NODE_MESH_OK;
}
