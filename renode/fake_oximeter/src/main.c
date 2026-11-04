/* A fake oximeter for the Renode tests (T-08). It offers a gateway what the real one does, the same
 * characteristics and the real one's device ID, which any central can read, but it has no pairing
 * key: it serves its own out-of-band data unsealed where the sealed data goes, and sends a reading
 * of its own to any central that subscribes. */
#include "medsec_bt.h"
#include "pairing_seal.h"
#include "plx.h"

#include <string.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(fake_oximeter, LOG_LEVEL_INF);

static const struct bt_uuid_128 alarm_service = BT_UUID_INIT_128(MEDSEC_ALARM_SERVICE_VAL);
static const struct bt_uuid_128 alarm_limits = BT_UUID_INIT_128(MEDSEC_ALARM_LIMITS_VAL);
static const struct bt_uuid_128 alarm_state = BT_UUID_INIT_128(MEDSEC_ALARM_STATE_VAL);
static const struct bt_uuid_128 alarm_challenge = BT_UUID_INIT_128(MEDSEC_ALARM_CHALLENGE_VAL);
static const struct bt_uuid_128 security_log = BT_UUID_INIT_128(MEDSEC_SECURITY_LOG_VAL);
static const struct bt_uuid_128 log_head = BT_UUID_INIT_128(MEDSEC_LOG_HEAD_VAL);
static const struct bt_uuid_128 device_id_uuid = BT_UUID_INIT_128(MEDSEC_DEVICE_ID_VAL);
static const struct bt_uuid_128 pairing_service = BT_UUID_INIT_128(MEDSEC_PAIRING_SERVICE_VAL);
static const struct bt_uuid_128 sealed_uuid = BT_UUID_INIT_128(MEDSEC_SEALED_PAIRING_DATA_VAL);

/* The real oximeter's, as the Renode tests provision it. */
static const uint8_t device_id[DEVICE_ID_SIZE] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};

/* A reading the patient's wouldn't give. */
static const oximetry_reading_t fake_reading = {
    .valid = true, .spo2_percent = 99u, .pulse_bpm = 60u};

/* Used on the Bluetooth receive thread only. */
static struct bt_le_oob oob;
static uint8_t unsealed[PAIRING_SEALED_SIZE];

static ssize_t read_device_id(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                              uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, device_id, sizeof device_id);
}

/* Without the key, the data goes where the ciphertext would, between a nonce and a tag of zeros. */
static ssize_t read_unsealed(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                             uint16_t len, uint16_t offset)
{
    if (offset == 0u) {
        if (bt_le_oob_get_local(BT_ID_DEFAULT, &oob) != 0) {
            return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
        }
        (void)memset(unsealed, 0, sizeof unsealed);
        (void)memcpy(&unsealed[PAIRING_SEAL_NONCE_SIZE], &oob.le_sc_data, PAIRING_DATA_SIZE);
    }
    return bt_gatt_attr_read(conn, attr, buf, len, offset, unsealed, sizeof unsealed);
}

/* The characteristics a gateway looks for, by UUID: only the readings, the alarm state and the
 * pairing data do anything. */
BT_GATT_SERVICE_DEFINE(fake_pulse_oximeter, BT_GATT_PRIMARY_SERVICE(BT_UUID_POS),
                       BT_GATT_CHARACTERISTIC(BT_UUID_GATT_PLX_CM, BT_GATT_CHRC_NOTIFY,
                                              BT_GATT_PERM_NONE, NULL, NULL, NULL),
                       BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE));

/* attrs[0] declares the service; attrs[1] is the Continuous Measurement characteristic. */
#define CONTINUOUS_MEASUREMENT (&fake_pulse_oximeter.attrs[1])

BT_GATT_SERVICE_DEFINE(fake_alarm, BT_GATT_PRIMARY_SERVICE(&alarm_service),
                       BT_GATT_CHARACTERISTIC(&alarm_limits.uuid, BT_GATT_CHRC_READ,
                                              BT_GATT_PERM_NONE, NULL, NULL, NULL),
                       BT_GATT_CHARACTERISTIC(&alarm_state.uuid, BT_GATT_CHRC_NOTIFY,
                                              BT_GATT_PERM_NONE, NULL, NULL, NULL),
                       BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
                       BT_GATT_CHARACTERISTIC(&alarm_challenge.uuid, BT_GATT_CHRC_READ,
                                              BT_GATT_PERM_NONE, NULL, NULL, NULL),
                       BT_GATT_CHARACTERISTIC(&security_log.uuid, BT_GATT_CHRC_READ,
                                              BT_GATT_PERM_NONE, NULL, NULL, NULL),
                       BT_GATT_CHARACTERISTIC(&log_head.uuid, BT_GATT_CHRC_READ, BT_GATT_PERM_NONE,
                                              NULL, NULL, NULL),
                       BT_GATT_CHARACTERISTIC(&device_id_uuid.uuid, BT_GATT_CHRC_READ,
                                              BT_GATT_PERM_READ, read_device_id, NULL, NULL));

BT_GATT_SERVICE_DEFINE(fake_pairing, BT_GATT_PRIMARY_SERVICE(&pairing_service),
                       BT_GATT_CHARACTERISTIC(&sealed_uuid.uuid, BT_GATT_CHRC_READ,
                                              BT_GATT_PERM_READ, read_unsealed, NULL, NULL));

static void on_oob_data_request(struct bt_conn *conn, struct bt_conn_oob_info *info)
{
    ARG_UNUSED(info);
    if (bt_le_oob_set_sc_data(conn, &oob.le_sc_data, NULL) != 0) {
        (void)bt_conn_auth_cancel(conn);
    }
}

static struct bt_conn_auth_cb auth_callbacks = {
    .oob_data_request = on_oob_data_request,
};

static const struct bt_data advertising[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
    BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_POS_VAL)),
};

static void advertise(void)
{
    const int err =
        bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, advertising, ARRAY_SIZE(advertising), NULL, 0u);

    if (err != 0) {
        LOG_ERR("advertising failed: %d", err);
    }
}

/* Advertising stops when a central connects, and needs that connection's object back to start. */
BT_CONN_CB_DEFINE(connection_callbacks) = {
    .recycled = advertise,
};

int main(void)
{
    uint8_t value[PLX_CONTINUOUS_SIZE];

    if ((bt_conn_auth_cb_register(&auth_callbacks) != 0) || (bt_enable(NULL) != 0)) {
        LOG_ERR("Bluetooth didn't start");
        return 0;
    }
    advertise();
    plx_encode_continuous(&fake_reading, value);
    while (true) {
        k_sleep(K_SECONDS(1));
        (void)bt_gatt_notify(NULL, CONTINUOUS_MEASUREMENT, value, sizeof value);
    }
}
