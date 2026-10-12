using Antmicro.Renode.Core;
using Antmicro.Renode.Core.Structure.Registers;
using Antmicro.Renode.Logging;
using Antmicro.Renode.Peripherals.Memory;

namespace Antmicro.Renode.Peripherals.Miscellaneous
{
    // The nRF52840's non-volatile memory controller (NVMC): what Zephyr's flash driver needs to
    // erase pages of the flash, which Renode maps as plain memory. Operations finish at once, so the
    // controller is always ready.
    //
    // Writes reach the flash directly, as the CPU stores to that memory, so the model doesn't
    // enforce what the hardware does with them: ignore them unless CONFIG enables writing, and
    // only ever clear bits. Erasing is what it adds.
    public class NRF52840_NVMC : BasicDoubleWordPeripheral, IKnownSize
    {
        public NRF52840_NVMC(IMachine machine, MappedMemory flash) : base(machine)
        {
            this.flash = flash;
            DefineRegisters();
        }

        public long Size => 0x1000;

        private void DefineRegisters()
        {
            Registers.Ready.Define(this)
                .WithFlag(0, FieldMode.Read, valueProviderCallback: _ => true, name: "READY")
                .WithReservedBits(1, 31);

            Registers.ReadyNext.Define(this)
                .WithFlag(0, FieldMode.Read, valueProviderCallback: _ => true, name: "READYNEXT")
                .WithReservedBits(1, 31);

            Registers.Config.Define(this)
                .WithEnumField(0, 2, out mode, name: "WEN")
                .WithReservedBits(2, 30);

            // ERASEPCR1 at the same address is the older name for the same register.
            Registers.ErasePage.Define(this)
                .WithValueField(0, 32, FieldMode.Write, writeCallback: (_, address) => ErasePage(address), name: "ERASEPAGE");

            Registers.EraseAll.Define(this)
                .WithFlag(0, FieldMode.Write, writeCallback: (_, value) => { if(value) EraseAll(); }, name: "ERASEALL")
                .WithReservedBits(1, 31);

            Registers.ErasePcr0.Define(this)
                .WithValueField(0, 32, FieldMode.Write, writeCallback: (_, address) => ErasePage(address), name: "ERASEPCR0");

            Registers.EraseUicr.Define(this)
                .WithFlag(0, FieldMode.Write, writeCallback: (_, value) =>
                {
                    if(value)
                    {
                        this.Log(LogLevel.Warning, "UICR isn't modeled; ERASEUICR does nothing");
                    }
                }, name: "ERASEUICR")
                .WithReservedBits(1, 31);

            // A partial erase splits the 85 ms page erase into short steps; here a step erases the
            // whole page, and the steps after it erase it again.
            Registers.ErasePagePartial.Define(this)
                .WithValueField(0, 32, FieldMode.Write, writeCallback: (_, address) => ErasePage(address), name: "ERASEPAGEPARTIAL");

            Registers.ErasePagePartialConfig.Define(this, 0xA)
                .WithValueField(0, 7, name: "DURATION")
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

        private void ErasePage(ulong address)
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
            flash.SetRange((long)address, PageSize, ErasedByte);
        }

        private void EraseAll()
        {
            if(EraseEnabled())
            {
                flash.SetRange(0, flash.Size, ErasedByte);
            }
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

        private readonly MappedMemory flash;

        private const int PageSize = 0x1000;
        private const byte ErasedByte = 0xFF;

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
