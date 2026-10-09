#include "oximetry.h"
#include "plxs.h"

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(oximeter, LOG_LEVEL_INF);

#define SENSOR_NODE DT_NODELABEL(max30101)
/* Samples per second reaching the FIFO: the sample rate after on-chip averaging. */
#define SAMPLE_RATE_HZ (DT_PROP(SENSOR_NODE, smp_sr) / DT_PROP(SENSOR_NODE, smp_ave))

BUILD_ASSERT(SAMPLE_RATE_HZ == OXIMETRY_SAMPLE_RATE_HZ,
             "the oximetry analysis expects the sensor at OXIMETRY_SAMPLE_RATE_HZ");

static const struct device *const sensor = DEVICE_DT_GET(SENSOR_NODE);
static oximetry_t oximetry;

/* Runs on the sensor driver's thread, once per sample, when the sensor's INT line falls. */
static void on_sample(const struct device *dev, const struct sensor_trigger *trigger)
{
    struct sensor_value red;
    struct sensor_value infrared;
    oximetry_reading_t reading;

    ARG_UNUSED(trigger);
    if ((sensor_sample_fetch(dev) != 0) || (sensor_channel_get(dev, SENSOR_CHAN_RED, &red) != 0) ||
        (sensor_channel_get(dev, SENSOR_CHAN_IR, &infrared) != 0)) {
        LOG_WRN("sample lost");
        return;
    }

    /* The driver reports 18-bit counts, never negative. */
    const oximetry_sample_t sample = {
        .red = (uint32_t)red.val1,
        .infrared = (uint32_t)infrared.val1,
    };

    if (!oximetry_add_sample(&oximetry, sample, &reading)) {
        return;
    }
    if (reading.valid) {
        LOG_INF("SpO2 %u%%, pulse %u bpm", reading.spo2_percent, reading.pulse_bpm);
    } else {
        LOG_INF("no pulse found");
    }
    plxs_send(&reading);
}

int main(void)
{
    static const struct sensor_trigger data_ready = {
        .type = SENSOR_TRIG_DATA_READY,
        .chan = SENSOR_CHAN_RED,
    };

    LOG_INF("medsec-oximeter started");
    if (!device_is_ready(sensor)) {
        LOG_ERR("pulse oximetry sensor not found");
        return 0;
    }
    oximetry_init(&oximetry);
    if (plxs_start() != 0) {
        LOG_ERR("could not start Bluetooth");
        return 0;
    }
    if (sensor_trigger_set(sensor, &data_ready, on_sample) != 0) {
        LOG_ERR("could not enable the sensor's data-ready interrupt");
    }
    return 0;
}
