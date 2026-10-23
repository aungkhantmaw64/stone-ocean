#pragma once

/**
 * @file node_mesh_msg.h
 * @brief Payloads of the Stone Ocean Node Server vendor model (spec 11).
 *        Not part of the public interface.
 *
 * Pure logic, built on the target and on the host: encodes and decodes the
 * protobuf messages of proto/stone_ocean/mesh/v1/node.proto with nanopb.
 * A payload is the message alone; its opcode travels separately. Callers
 * never see the generated nanopb types. The header must not include
 * ESP-IDF headers.
 */

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "node_config.h"
#include "node_dev.h"
#include "node_mesh_err.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Most payload bytes of an unsegmented vendor message: 11 bytes of
 *         access payload less the 3-byte vendor opcode. Every message but
 *         DeviceList fits in it. */
#define NODE_MESH_MSG_MAX_UNSEGMENTED 8

/** @brief Largest DeviceList payload, in bytes: NODE_CONFIG_MAX_DEVICES
 *         devices with the longest ids. Sent segmented. */
#define NODE_MESH_MSG_DEVICE_LIST_MAX_LEN 168

/** @brief Opcode number of DeviceListGet, gateway to node. The vendor
 *         opcode is 0xC0 | op followed by the company ID. */
#define NODE_MESH_MSG_OP_DEVICE_LIST_GET 0x01

/** @brief Opcode number of DeviceList, node to gateway. */
#define NODE_MESH_MSG_OP_DEVICE_LIST 0x02

/** @brief Opcode number of DeviceGet, gateway to node. */
#define NODE_MESH_MSG_OP_DEVICE_GET 0x03

/** @brief Opcode number of DeviceStatus, node to gateway. */
#define NODE_MESH_MSG_OP_DEVICE_STATUS 0x04

/** @brief Opcode number of the acknowledged OnOffSet, gateway to node. */
#define NODE_MESH_MSG_OP_ONOFF_SET 0x05

/** @brief Opcode number of the unacknowledged OnOffSet, gateway to node; same
 *         payload as NODE_MESH_MSG_OP_ONOFF_SET. */
#define NODE_MESH_MSG_OP_ONOFF_SET_UNACK 0x06

/** @brief Lowest SHTC3 temperature sent, in °C: the bottom of the sensor's
 *         conversion formula T = -45 + 175 * raw / 2^16 (datasheet). */
#define NODE_MESH_MSG_TEMP_MIN_C (-45.0f)

/** @brief Highest SHTC3 temperature sent, in °C: the top of the sensor's
 *         conversion formula T = -45 + 175 * raw / 2^16 (datasheet). */
#define NODE_MESH_MSG_TEMP_MAX_C 130.0f

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/**
 * @brief Encode a DeviceStatus payload.
 *
 * Only the value fields of @p type are written. With @p fault set, or no
 * @p value, no value fields are written: the gateway reads them as unknown.
 * Temperature is clamped to NODE_MESH_MSG_TEMP_MIN_C to
 * NODE_MESH_MSG_TEMP_MAX_C and humidity to 0 to 100 %, then both are rounded
 * half away from zero to 0.1 units. A temperature or humidity that is not a
 * finite number is left out. The payload is at most
 * NODE_MESH_MSG_MAX_UNSEGMENTED bytes, and may be empty.
 *
 * @param dev_index Device index, 0 to NODE_CONFIG_MAX_DEVICES - 1.
 * @param type      Device type.
 * @param value     The device's latest value, or NULL if it has none.
 * @param fault     True if the device is in FAULT.
 * @param out       Receives the payload; must not be NULL.
 * @param cap       Size of @p out in bytes.
 * @param len       Receives the payload length in bytes; 0 on failure. Must
 *                  not be NULL.
 * @return NODE_MESH_OK, or NODE_MESH_ERR_ARG for a NULL pointer, a device
 *         index or type out of range, or a buffer too small.
 */
node_mesh_err_t node_mesh_msg_encode_device_status(
    int dev_index, node_config_dev_type_t type, const node_dev_value_t *value,
    bool fault, uint8_t *out, size_t cap, size_t *len);

/**
 * @brief Encode a DeviceList payload: every device of the config, in config
 *        order, with its type and id.
 *
 * @param cfg Active config, or NULL in safe mode, which gives an empty list.
 * @param out Receives the payload; must not be NULL.
 * @param cap Size of @p out in bytes; NODE_MESH_MSG_DEVICE_LIST_MAX_LEN is
 *            always enough.
 * @param len Receives the payload length in bytes; 0 on failure. Must not be
 *            NULL.
 * @return NODE_MESH_OK, or NODE_MESH_ERR_ARG for a NULL pointer, a device
 *         count or type out of range, or a buffer too small.
 */
node_mesh_err_t node_mesh_msg_encode_device_list(const node_config_t *cfg,
                                                 uint8_t *out, size_t cap,
                                                 size_t *len);

/**
 * @brief Decode a DeviceGet payload.
 *
 * Fields unknown to this firmware are skipped.
 *
 * @param in        Payload; may be NULL if @p len is 0.
 * @param len       Payload length in bytes, at most
 *                  NODE_MESH_MSG_MAX_UNSEGMENTED.
 * @param all       Receives true if every device is asked for; must not be
 *                  NULL.
 * @param dev_index Receives the device index asked for, 0 to
 *                  NODE_CONFIG_MAX_DEVICES - 1; 0 when @p all is true. Must
 *                  not be NULL.
 * @return NODE_MESH_OK, or NODE_MESH_ERR_ARG for a NULL pointer, a payload
 *         that is too long or does not decode, or a device index out of
 *         range. Outputs are false and 0 on failure.
 */
node_mesh_err_t node_mesh_msg_decode_device_get(const uint8_t *in, size_t len,
                                                bool *all, int *dev_index);

/**
 * @brief Decode an OnOffSet payload, acknowledged or not.
 *
 * Fields unknown to this firmware are skipped.
 *
 * @param in        Payload; may be NULL if @p len is 0.
 * @param len       Payload length in bytes, at most
 *                  NODE_MESH_MSG_MAX_UNSEGMENTED.
 * @param dev_index Receives the device index, 0 to
 *                  NODE_CONFIG_MAX_DEVICES - 1. Must not be NULL.
 * @param on        Receives true to switch on. Must not be NULL.
 * @return NODE_MESH_OK, or NODE_MESH_ERR_ARG for a NULL pointer, a payload
 *         that is too long or does not decode, or a device index out of
 *         range. Outputs are 0 and false on failure.
 */
node_mesh_err_t node_mesh_msg_decode_onoff_set(const uint8_t *in, size_t len,
                                               int *dev_index, bool *on);
