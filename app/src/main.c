#include "alarm_service.h"
#include "device_identity.h"
#include "event_log.h"
#include "log_mac.h"
#include "oximetry.h"
#include "pairing.h"
#include "pairing_seal.h"
#include "plx_service.h"
#include "sensor_fifo.h"
#include "supervisor.h"
#include "update_guard.h"
#include "update_log.h"

#include <mbedtls/platform_util.h>
#include <zephyr/device.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(oximeter, LOG_LEVEL_INF);

#define SENSOR_NODE DT_NODELABEL(max30101)
/* FIFO_WR_PTR, then OVF_COUNTER and FIFO_RD_PTR: what sensor_fifo_t holds. */
#define SENSOR_FIFO_REGISTERS 0x04u
/* Samples per second reaching the FIFO: the sample rate after on-chip averaging. */
#define SAMPLE_RATE_HZ (DT_PROP(SENSOR_NODE, smp_sr) / DT_PROP(SENSOR_NODE, smp_ave))

BUILD_ASSERT(SAMPLE_RATE_HZ == OXIMETRY_SAMPLE_RATE_HZ,
             "the oximetry analysis expects the sensor at OXIMETRY_SAMPLE_RATE_HZ");

static const struct device *const sensor = DEVICE_DT_GET(SENSOR_NODE);
static const struct i2c_dt_spec sensor_bus = I2C_DT_SPEC_GET(SENSOR_NODE);
static oximetry_t oximetry;

/* Called with each reading: the self-test an update must pass. The sensor answered with its part
 * ID, Bluetooth is up, since main() starts the sensor after it, and the analysis has run. Until the
 * image is marked as working, a reset brings back the one before it. A failed write is tried again
 * with the next reading. */
static void confirm_image(void)
{
    static bool confirmed;

    if (confirmed) {
        return;
    }
    if (!boot_is_img_confirmed()) {
        if (boot_write_img_confirmed() != 0) {
            LOG_ERR("could not mark the image as working");
            return;
        }
        LOG_INF("image marked as working");
    }
    confirmed = true;
}

/* Takes the oldest sample in the sensor's FIFO into the analysis, and sends the reading once a
 * second. */
static void take_sample(const struct device *dev)
{
    struct sensor_value red;
    struct sensor_value infrared;
    oximetry_reading_t reading;

    if ((sensor_sample_fetch(dev) != 0) || (sensor_channel_get(dev, SENSOR_CHAN_RED, &red) != 0) ||
        (sensor_channel_get(dev, SENSOR_CHAN_IR, &infrared) != 0)) {
        LOG_WRN("sample lost");
        return;
    }
    supervisor_sample();

    /* The driver reports 18-bit counts, never negative. */
    const oximetry_sample_t sample = {
        .red = (uint32_t)red.val1,
        .infrared = (uint32_t)infrared.val1,
    };

    if (!oximetry_add_sample(&oximetry, sample, &reading)) {
        return;
    }
    confirm_image();
    if (reading.valid) {
        LOG_INF("SpO2 %u%%, pulse %u bpm", reading.spo2_percent, reading.pulse_bpm);
    } else {
        LOG_INF("no pulse found");
    }
    plx_service_send(&reading);
    alarm_service_update(&reading);
}

/* Runs on the sensor driver's thread when the sensor's INT line falls: a new sample is in its FIFO.
 * Whatever held this thread up may have left more, so all of them are read. Reading one an
 * interrupt would leave the rest waiting for good, until the FIFO filled and dropped new ones. */
static void on_sample(const struct device *dev, const struct sensor_trigger *trigger)
{
    uint8_t registers[3];

    ARG_UNUSED(trigger);
    if (i2c_burst_read_dt(&sensor_bus, SENSOR_FIFO_REGISTERS, registers, sizeof registers) != 0) {
        LOG_WRN("could not read the sensor's FIFO");
        return;
    }
    const sensor_fifo_t fifo = {
        .write_pointer = registers[0],
        .overflow = registers[1],
        .read_pointer = registers[2],
    };

    for (uint32_t waiting = sensor_fifo_waiting(fifo); waiting > 0u; waiting--) {
        take_sample(dev);
    }
}

/* With no samples there's nothing to measure, so a stopped sensor reads as no reading: the
 * technical alarm sounds and the hub hears it, rather than everything falling silent. */
static void on_stall(void)
{
    static const oximetry_reading_t none = {.valid = false};

    LOG_WRN("no samples from the sensor");
    plx_service_send(&none);
    alarm_service_update(&none);
}

/* Logs the version MCUboot checked, from the signed header of the slot this image runs from, and
 * returns it: zero if the header can't be read. */
static struct mcuboot_img_sem_ver log_start(void)
{
    struct mcuboot_img_header header;

    if (boot_read_bank_header(boot_fetch_active_slot(), &header, sizeof header) != 0) {
        LOG_INF("medsec-oximeter started; its image header can't be read");
        return (struct mcuboot_img_sem_ver){0};
    }
    const struct mcuboot_img_sem_ver version = header.h.v1.sem_ver;

    LOG_INF("medsec-oximeter %u.%u.%u+%u started", version.major, version.minor, version.revision,
            version.build_num);
    return version;
}

/* A key from UICR into the module that uses it, wiping the copy here. */
static void load_key(device_key_purpose_t purpose, bool (*init)(const uint8_t *key, size_t length),
                     const char *name)
{
    uint8_t key[DEVICE_KEY_SIZE];

    if (device_identity_key(purpose, key) && !init(key, sizeof key)) {
        LOG_ERR("could not load the %s key", name);
    }
    mbedtls_platform_zeroize(key, sizeof key);
}

int main(void)
{
    static const struct sensor_trigger data_ready = {
        .type = SENSOR_TRIG_DATA_READY,
        .chan = SENSOR_CHAN_RED,
    };

    const struct mcuboot_img_sem_ver version = log_start();

    /* Before the log starts, so even the start-up record is tagged. Without a key, records are
     * still kept, with tags that verify against nothing; without a pairing key, nothing can
     * pair. */
    load_key(DEVICE_KEY_LOG, log_mac_init, "log");
    load_key(DEVICE_KEY_PAIRING, pairing_seal_init, "pairing");
    /* Monitoring goes on without the log if it can't start. */
    if (event_log_start(&version) != 0) {
        LOG_ERR("security event log unavailable");
    }
    oximetry_init(&oximetry);
    if (alarm_service_init() != 0) {
        LOG_ERR("could not set up the alarm LED");
        return 0;
    }
    if (pairing_init() != 0) {
        LOG_ERR("could not set up pairing");
        return 0;
    }
    update_guard_init();
    update_log_init();
    if (plx_service_start() != 0) {
        LOG_ERR("could not start Bluetooth");
        return 0;
    }
    /* Started before the sensor, so a sensor that never sends a sample raises the alarm too. */
    if (supervisor_start(on_stall) != 0) {
        LOG_ERR("could not start the watchdog");
        return 0;
    }
    if (!device_is_ready(sensor) || (sensor_trigger_set(sensor, &data_ready, on_sample) != 0)) {
        LOG_ERR("pulse oximetry sensor not working");
    }
    return 0;
}
