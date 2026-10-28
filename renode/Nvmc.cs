using Antmicro.Renode.Core;
using Antmicro.Renode.Core.Structure.Registers;
using Antmicro.Renode.Logging;
using Antmicro.Renode.Peripherals.CPU;
using Antmicro.Renode.Peripherals.Memory;
using Antmicro.Renode.Peripherals.Timers;
using Antmicro.Renode.Time;

namespace Antmicro.Renode.Peripherals.Miscellaneous
{
    // The nRF52840's non-volatile memory controller (NVMC): what Zephyr's flash driver needs to
    // erase pages of the flash, which Renode maps as plain memory. An erase takes as long as the
    // product specification says and halts the CPU meanwhile, as the chip halts a CPU that runs
    // from flash: that stall is what firmware has to live with while it erases.
    //
    // Writes reach the flash directly, as the CPU stores to that memory, so the model doesn't
    // enforce what the hardware does with them: ignore them unless CONFIG enables writing, and
    // only ever clear bits. Nor does it time them (41 us a word on the chip). Erasing is what it
    // adds.
    public class NRF52840_NVMC : BasicDoubleWordPeripheral, IKnownSize
    {
        public NRF52840_NVMC(IMachine machine, MappedMemory flash, ICPU cpu) : base(machine)
        {
            this.flash = flash;
            this.cpu = cpu;
            eraseTimer = new LimitTimer(machine.ClockSource, 1000000, this, "erase", direction: Direction.Ascending,
                workMode: WorkMode.OneShot, eventEnabled: true);
            eraseTimer.LimitReached += FinishErase;
            DefineRegisters();
        }

        public override void Reset()
        {
            base.Reset();
            eraseTimer.Reset();
            // A reset in the middle of an erase leaves the page as it was here (part erased on the
            // chip), and the CPU free to run.
            if(erasing)
            {
                erasing = false;
                cpu.IsHalted = false;
            }
        }

        public long Size => 0x1000;

        private void DefineRegisters()
        {
            Registers.Ready.Define(this)
                .WithFlag(0, FieldMode.Read, valueProviderCallback: _ => !erasing, name: "READY")
                .WithReservedBits(1, 31);

            Registers.ReadyNext.Define(this)
                .WithFlag(0, FieldMode.Read, valueProviderCallback: _ => !erasing, name: "READYNEXT")
                .WithReservedBits(1, 31);

            Registers.Config.Define(this)
                .WithEnumField(0, 2, out mode, name: "WEN")
                .WithReservedBits(2, 30);

            // ERASEPCR1 at the same address is the older name for the same register.
            Registers.ErasePage.Define(this)
                .WithValueField(0, 32, FieldMode.Write, writeCallback: (_, address) => ErasePage(address, PageEraseMicroseconds), name: "ERASEPAGE");

            Registers.EraseAll.Define(this)
                .WithFlag(0, FieldMode.Write, writeCallback: (_, value) => { if(value) EraseAll(); }, name: "ERASEALL")
                .WithReservedBits(1, 31);

            Registers.ErasePcr0.Define(this)
                .WithValueField(0, 32, FieldMode.Write, writeCallback: (_, address) => ErasePage(address, PageEraseMicroseconds), name: "ERASEPCR0");

            Registers.EraseUicr.Define(this)
                .WithFlag(0, FieldMode.Write, writeCallback: (_, value) =>
                {
                    if(value)
                    {
                        this.Log(LogLevel.Warning, "Erasing UICR isn't modeled; ERASEUICR does nothing");
                    }
                }, name: "ERASEUICR")
                .WithReservedBits(1, 31);

            // A partial erase splits the page erase into steps of DURATION ms; here each step erases
            // the whole page, and the steps after it erase it again.
            Registers.ErasePagePartial.Define(this)
                .WithValueField(0, 32, FieldMode.Write,
                    writeCallback: (_, address) => ErasePage(address, partialEraseMilliseconds.Value * 1000), name: "ERASEPAGEPARTIAL");

            Registers.ErasePagePartialConfig.Define(this, 0xA)
                .WithValueField(0, 7, out partialEraseMilliseconds, name: "DURATION")
                .WithReservedBits(7, 25);

            Registers.InstructionCacheConfig.Define(this)
                .WithFlag(0, name: "CACHEEN")
                .WithReservedBits(1, 7)
                .WithFlag(8, name: "CACHEPROFEN")
                .WithReservedBits(9, 23);

            Registers.InstructionCacheHits.Define(this)
                .WithValueField(0, 32, name: "HITS");

            Registers.InstructionCacheMisses.Define(this)
                .WithValueField(0, 32, name: "MISSES");
        }

        private void ErasePage(ulong address, ulong microseconds)
        {
            if(!EraseEnabled())
            {
                return;
            }
            if(address % PageSize != 0 || address >= (ulong)flash.Size)
            {
                this.Log(LogLevel.Warning, "Erase of 0x{0:X}, not the start of a flash page, ignored", address);
                return;
            }
            StartErase((long)address, PageSize, microseconds);
        }

        private void EraseAll()
        {
            if(EraseEnabled())
            {
                StartErase(0, flash.Size, EraseAllMicroseconds);
            }
        }

        private void StartErase(long start, long length, ulong microseconds)
        {
            erasing = true;
            eraseStart = start;
            eraseLength = length;
            cpu.IsHalted = true;
            eraseTimer.Limit = microseconds;
            eraseTimer.Value = 0;
            eraseTimer.Enabled = true;
        }

        private void FinishErase()
        {
            flash.SetRange(eraseStart, eraseLength, ErasedByte);
            erasing = false;
            cpu.IsHalted = false;
        }

        // The hardware ignores an erase unless CONFIG enables erasing; firmware that forgets is
        // worth a warning.
        private bool EraseEnabled()
        {
            if(mode.Value != Mode.EraseEnabled)
            {
                this.Log(LogLevel.Warning, "Erase requested while CONFIG.WEN is {0}; ignored", mode.Value);
                return false;
            }
            return true;
        }

        private IEnumRegisterField<Mode> mode;
        private IValueRegisterField partialEraseMilliseconds;
        private bool erasing;
        private long eraseStart;
        private long eraseLength;

        private readonly MappedMemory flash;
        private readonly ICPU cpu;
        private readonly LimitTimer eraseTimer;

        private const int PageSize = 0x1000;
        private const byte ErasedByte = 0xFF;
        // tERASEPAGE and tERASEALL, the nRF52840 product specification's NVMC electrical
        // specification.
        private const ulong PageEraseMicroseconds = 85000;
        private const ulong EraseAllMicroseconds = 169000;

        private enum Mode
        {
            ReadOnly = 0,
            WriteEnabled = 1,
            EraseEnabled = 2,
        }

        private enum Registers : long
        {
            Ready = 0x400,
            ReadyNext = 0x408,
            Config = 0x504,
            ErasePage = 0x508,
            EraseAll = 0x50C,
            ErasePcr0 = 0x510,
            EraseUicr = 0x514,
            ErasePagePartial = 0x518,
            ErasePagePartialConfig = 0x51C,
            InstructionCacheConfig = 0x540,
            InstructionCacheHits = 0x548,
            InstructionCacheMisses = 0x54C,
        }
    }
}
