#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(oximeter, LOG_LEVEL_INF);

#define SENSOR_NODE DT_NODELABEL(max30101)
/* Samples per second reaching the FIFO: the sample rate after on-chip averaging. */
#define SAMPLE_RATE_HZ (DT_PROP(SENSOR_NODE, smp_sr) / DT_PROP(SENSOR_NODE, smp_ave))

static const struct device *const sensor = DEVICE_DT_GET(SENSOR_NODE);

/* One second of samples, summed to log the light level each LED sees. */
static struct {
    uint32_t count;
    uint64_t red;
    uint64_t infrared;
} window;

/* Runs on the system work queue, once per sample, when the sensor's INT line falls. */
static void on_sample(const struct device *dev, const struct sensor_trigger *trigger)
{
    struct sensor_value red;
    struct sensor_value infrared;

    ARG_UNUSED(trigger);
    if ((sensor_sample_fetch(dev) != 0) || (sensor_channel_get(dev, SENSOR_CHAN_RED, &red) != 0) ||
        (sensor_channel_get(dev, SENSOR_CHAN_IR, &infrared) != 0)) {
        LOG_WRN("sample lost");
        return;
    }

    window.red += (uint32_t)red.val1;
    window.infrared += (uint32_t)infrared.val1;
    window.count++;
    if (window.count == SAMPLE_RATE_HZ) {
        LOG_INF("ppg: red %u, ir %u", (unsigned int)(window.red / window.count),
                (unsigned int)(window.infrared / window.count));
        window.count = 0;
        window.red = 0;
        window.infrared = 0;
    }
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
    if (sensor_trigger_set(sensor, &data_ready, on_sample) != 0) {
        LOG_ERR("could not enable the sensor's data-ready interrupt");
    }
    return 0;
}
