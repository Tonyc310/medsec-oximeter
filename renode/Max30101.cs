using System;

using Antmicro.Renode.Core;
using Antmicro.Renode.Logging;
using Antmicro.Renode.Peripherals.I2C;
using Antmicro.Renode.Peripherals.Timers;
using Antmicro.Renode.Time;

namespace Antmicro.Renode.Peripherals.Sensors
{
    // MAX30101 pulse oximetry and heart-rate sensor on I2C: the registers Zephyr's driver uses in
    // SpO2 mode, a 32-sample FIFO filled at the configured rate, and the active-low INT pin.
    // Anything else is logged as unmodeled.
    //
    // Each sample is a synthetic photoplethysmogram (PPG): the light that comes back through the
    // tissue (the level) minus a dip with every heartbeat, when arterial blood absorbs more of it
    // (the pulse). An oximeter turns the ratio of the red and infrared pulses, each relative to its
    // level, into SpO2. The defaults are a healthy adult at 72 bpm.
    public class MAX30101 : II2CPeripheral
    {
        public MAX30101(IMachine machine)
        {
            sampler = new LimitTimer(machine.ClockSource, 1, this, "sampler", limit: 1, direction: Direction.Ascending,
                workMode: WorkMode.Periodic, eventEnabled: true);
            sampler.LimitReached += TakeSample;
            fifo = new Sample[FifoDepth];
            IRQ = new GPIO();
            HeartRate = 72;
            RedLevel = 110000;
            RedPulse = 880;
            InfraredLevel = 130000;
            InfraredPulse = 2000;
            Reset();
        }

        public void Reset()
        {
            sampler.Enabled = false;
            address = 0;
            expectingAddress = true;
            fifoByte = 0;
            interruptEnable = 0;
            sampleReady = false;
            writePointer = 0;
            readPointer = 0;
            unread = 0;
            fifoConfig = 0;
            modeConfig = 0;
            spo2Config = 0;
            Array.Clear(ledAmplitude, 0, ledAmplitude.Length);
            sampleIndex = 0;
            // Power-on sets PWR_RDY, which can't be masked, so INT stays low until the status is
            // read. The reset bit goes through the same power-on reset, so it sets PWR_RDY too.
            powerReady = true;
            UpdateInterrupt();
        }

        // The first byte of a write selects the register; later bytes write it and count up from there.
        public void Write(byte[] data)
        {
            foreach(var b in data)
            {
                if(expectingAddress)
                {
                    address = b;
                    expectingAddress = false;
                    fifoByte = 0;
                }
                else
                {
                    WriteRegister(address, b);
                    Advance();
                }
            }
        }

        public byte[] Read(int count = 1)
        {
            var result = new byte[count];
            for(var i = 0; i < count; i++)
            {
                result[i] = ReadRegister(address);
                Advance();
            }
            return result;
        }

        public void FinishTransmission()
        {
            expectingAddress = true;
        }

        public GPIO IRQ { get; }

        // Beats per minute.
        public double HeartRate { get; set; }

        // ADC counts at 18-bit resolution, before the heartbeat's dip.
        public uint RedLevel { get; set; }

        // ADC counts: the depth of the heartbeat's dip.
        public uint RedPulse { get; set; }

        public uint InfraredLevel { get; set; }

        public uint InfraredPulse { get; set; }

        private void Advance()
        {
            // Reads of FIFO_DATA stay on it, so a burst read drains whole samples.
            if(address != (byte)Register.FifoData)
            {
                address++;
            }
        }

        private byte ReadRegister(byte register)
        {
            switch((Register)register)
            {
            case Register.InterruptStatus1:
                var status = (byte)((sampleReady ? SampleReadyBit : 0) | (powerReady ? PowerReadyBit : 0));
                // Reading the status clears it and releases INT.
                sampleReady = powerReady = false;
                UpdateInterrupt();
                return status;
            case Register.InterruptEnable1:
                return interruptEnable;
            case Register.FifoData:
                return ReadFifoByte();
            case Register.FifoConfig:
                return fifoConfig;
            case Register.ModeConfig:
                return modeConfig;
            case Register.Spo2Config:
                return spo2Config;
            case Register.Led1Amplitude:
            case Register.Led2Amplitude:
            case Register.Led3Amplitude:
            case Register.Led4Amplitude:
                return ledAmplitude[register - (byte)Register.Led1Amplitude];
            case Register.PartId:
                return PartId;
            // Renode's TWI controller fetches the next byte as soon as one is taken, so reading one
            // register also reads the one after it. These follow registers the driver reads; with die
            // temperature and proximity sensing unused, they keep their reset value.
            case Register.InterruptStatus2:
            case Register.InterruptEnable2:
            case Register.PilotAmplitude:
                return 0;
            default:
                this.Log(LogLevel.Warning, "Read from unmodeled register 0x{0:X2}", register);
                return 0;
            }
        }

        private void WriteRegister(byte register, byte value)
        {
            switch((Register)register)
            {
            case Register.InterruptEnable1:
                interruptEnable = (byte)(value & 0xE0);
                UpdateInterrupt();
                break;
            case Register.FifoConfig:
                if((value & RolloverBit) != 0)
                {
                    this.Log(LogLevel.Warning, "FIFO rollover isn't modeled: a full FIFO drops new samples");
                }
                fifoConfig = value;
                Reconfigure();
                break;
            case Register.ModeConfig:
                if((value & ResetBit) != 0)
                {
                    // The reset bit clears itself once the registers are back at their defaults.
                    Reset();
                    break;
                }
                modeConfig = (byte)(value & 0x87);
                Reconfigure();
                break;
            case Register.Spo2Config:
                spo2Config = (byte)(value & 0x7F);
                Reconfigure();
                break;
            case Register.Led1Amplitude:
            case Register.Led2Amplitude:
            case Register.Led3Amplitude:
            case Register.Led4Amplitude:
                ledAmplitude[register - (byte)Register.Led1Amplitude] = value;
                break;
            default:
                this.Log(LogLevel.Warning, "Write of 0x{0:X2} to unmodeled register 0x{1:X2}", value, register);
                break;
            }
        }

        private void Reconfigure()
        {
            var mode = modeConfig & ModeMask;
            var running = (modeConfig & ShutdownBit) == 0 && (mode == HeartRateMode || mode == Spo2Mode);
            if(mode == MultiLedMode)
            {
                this.Log(LogLevel.Warning, "Multi-LED mode isn't modeled; no samples are taken");
            }
            // On-chip averaging decimates: 4-sample averaging at 400 sps fills the FIFO at 100 sps.
            var averaged = SampleRates[(spo2Config >> 2) & 0x07] >> Math.Min((fifoConfig >> 5) & 0x07, 5);
            sampler.Frequency = (ulong)Math.Max(averaged, 1);
            sampler.Enabled = running;
        }

        private void TakeSample()
        {
            if(unread == FifoDepth)
            {
                // Without rollover, the driver's default, a full FIFO keeps its oldest samples.
                return;
            }
            var phase = (sampleIndex * HeartRate / 60.0 / sampler.Frequency) % 1.0;
            sampleIndex++;
            var beat = Heartbeat(phase);
            fifo[writePointer] = new Sample
            {
                Red = Quantize(RedLevel - RedPulse * beat),
                Infrared = Quantize(InfraredLevel - InfraredPulse * beat),
            };
            writePointer = (writePointer + 1) % FifoDepth;
            unread++;
            sampleReady = true;
            UpdateInterrupt();
        }

        // One beat: a quick rise to the systolic peak a fifth of the way in, then a slower fall.
        private static double Heartbeat(double phase)
        {
            return phase < SystolicPeak
                ? Math.Sin(Math.PI / 2 * phase / SystolicPeak)
                : Math.Cos(Math.PI / 2 * (phase - SystolicPeak) / (1 - SystolicPeak));
        }

        // Samples are 18-bit and left-justified: shorter LED pulses give fewer bits, the low ones zero.
        private uint Quantize(double counts)
        {
            var value = (uint)Math.Max(0, Math.Min(counts, FullScale));
            var droppedBits = 3 - (spo2Config & 0x03);
            return value & ~((1u << droppedBits) - 1);
        }

        // A sample is 3 bytes per active LED, most significant first: red, then infrared in SpO2 mode.
        private byte ReadFifoByte()
        {
            var channels = (modeConfig & ModeMask) == Spo2Mode ? 2 : 1;
            var sample = fifo[readPointer];
            var value = fifoByte < 3 ? sample.Red : sample.Infrared;
            var shift = 16 - 8 * (fifoByte % 3);
            var result = (byte)(value >> shift);

            fifoByte++;
            if(fifoByte == 3 * channels)
            {
                fifoByte = 0;
                // An empty FIFO repeats its last sample without moving the read pointer.
                if(unread > 0)
                {
                    readPointer = (readPointer + 1) % FifoDepth;
                    unread--;
                }
            }
            return result;
        }

        private void UpdateInterrupt()
        {
            var pending = powerReady || (sampleReady && (interruptEnable & SampleReadyBit) != 0);
            // Active low: the pin idles high and drops while an enabled interrupt is pending.
            IRQ.Set(!pending);
        }

        private byte address;
        private bool expectingAddress;
        private int fifoByte;
        private byte interruptEnable;
        private bool sampleReady;
        private bool powerReady;
        private int writePointer;
        private int readPointer;
        private int unread;
        private byte fifoConfig;
        private byte modeConfig;
        private byte spo2Config;
        private ulong sampleIndex;

        private readonly LimitTimer sampler;
        private readonly Sample[] fifo;
        private readonly byte[] ledAmplitude = new byte[4];

        private const int FifoDepth = 32;
        private const byte SampleReadyBit = 0x40;
        private const byte PowerReadyBit = 0x01;
        private const byte ResetBit = 0x40;
        private const byte ShutdownBit = 0x80;
        private const byte RolloverBit = 0x10;
        private const int ModeMask = 0x07;
        private const int HeartRateMode = 0x02;
        private const int Spo2Mode = 0x03;
        private const int MultiLedMode = 0x07;
        private const double FullScale = (1 << 18) - 1;
        private const double SystolicPeak = 0.2;
        private const byte PartId = 0x15;

        // SPO2_SR codes 0-7, in samples per second.
        private static readonly int[] SampleRates = { 50, 100, 200, 400, 800, 1000, 1600, 3200 };

        private struct Sample
        {
            public uint Red;
            public uint Infrared;
        }

        private enum Register : byte
        {
            InterruptStatus1 = 0x00,
            InterruptStatus2 = 0x01,
            InterruptEnable1 = 0x02,
            InterruptEnable2 = 0x03,
            FifoData = 0x07,
            FifoConfig = 0x08,
            ModeConfig = 0x09,
            Spo2Config = 0x0A,
            Led1Amplitude = 0x0C,
            Led2Amplitude = 0x0D,
            Led3Amplitude = 0x0E,
            Led4Amplitude = 0x0F,
            PilotAmplitude = 0x10,
            PartId = 0xFF,
        }
    }
}
