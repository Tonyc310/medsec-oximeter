#include "paired_only.h"

#include "medsec_bt.h"

#include <stdlib.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/sys/iterable_sections.h>
#include <zephyr/sys/printk.h>

static const struct bt_uuid_128 device_id_uuid = BT_UUID_INIT_128(MEDSEC_DEVICE_ID_VAL);
static const struct bt_uuid_128 sealed_uuid = BT_UUID_INIT_128(MEDSEC_SEALED_PAIRING_DATA_VAL);

/* What a central reads to find the oximeter's services and pair. A descriptor's subscription state
 * is the reader's own, so reading it gives nothing away. */
static const struct bt_uuid *const open_to_read[] = {
    BT_UUID_GATT_PRIMARY, BT_UUID_GATT_CHRC, BT_UUID_GATT_CCC,
    &device_id_uuid.uuid, &sealed_uuid.uuid,
};

/* Zephyr's GAP and GATT services hold nothing of the oximeter's. Static services take consecutive
 * handles, the service declaration first; a handle in none of them is the oximeter's. */
static bool in_zephyr_service(const struct bt_gatt_attr *attr)
{
    const uint16_t handle = bt_gatt_attr_get_handle(attr);

    STRUCT_SECTION_FOREACH(bt_gatt_service_static, service) {
        const uint16_t first = bt_gatt_attr_get_handle(&service->attrs[0]);

        if ((handle >= first) && ((size_t)(handle - first) < service->attr_count)) {
            const struct bt_uuid *uuid = service->attrs[0].user_data;

            return (bt_uuid_cmp(uuid, BT_UUID_GAP) == 0) || (bt_uuid_cmp(uuid, BT_UUID_GATT) == 0);
        }
    }
    return false;
}

static bool open_before_pairing(const struct bt_gatt_attr *attr)
{
    for (size_t i = 0u; i < ARRAY_SIZE(open_to_read); i++) {
        if (bt_uuid_cmp(attr->uuid, open_to_read[i]) == 0) {
            return true;
        }
    }
    return false;
}

static void reached(const char *access, const struct bt_gatt_attr *attr)
{
    printk("C-04: a central below level 4 %s handle 0x%04x\n", access,
           bt_gatt_attr_get_handle(attr));
    abort();
}

/* Zephyr asks once an attribute's permissions allow the request, on every ATT path that reads or
 * writes a value, so these see whatever a permission lets through. */
static bool on_read(struct bt_conn *conn, const struct bt_gatt_attr *attr)
{
    if ((bt_conn_get_security(conn) < BT_SECURITY_L4) && !in_zephyr_service(attr) &&
        !open_before_pairing(attr)) {
        reached("read", attr);
    }
    return true;
}

static bool on_write(struct bt_conn *conn, const struct bt_gatt_attr *attr)
{
    if ((bt_conn_get_security(conn) < BT_SECURITY_L4) && !in_zephyr_service(attr)) {
        reached("wrote", attr);
    }
    return true;
}

static const struct bt_gatt_authorization_cb checks = {
    .read_authorize = on_read,
    .write_authorize = on_write,
};

void paired_only_watch(void)
{
    if (bt_gatt_authorization_cb_register(&checks) != 0) {
        printk("the C-04 check didn't start\n");
        abort();
    }
}
