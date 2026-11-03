using System;
using System.Linq;
using Antmicro.Renode.Core;
using Antmicro.Renode.Core.Structure.Registers;
using Antmicro.Renode.Utilities.Crypto;

namespace Antmicro.Renode.Peripherals.Miscellaneous
{
    // The nRF52840's AES CCM block (CCM): what Zephyr's Bluetooth controller needs to encrypt and
    // decrypt packets on an encrypted link. Operations finish at once: generating the key stream
    // only signals its end, and starts encryption through its shortcut. Nothing here fails, so
    // ERROR never fires. ENABLE, the data rate, the scratch area and the maximum packet size are
    // accepted and ignored.
    //
    // Packets in RAM are a header byte, a length byte and a spare byte (S1), then the payload: the
    // radio's layout with S1 included, which Zephyr uses on the nRF52840. Only the 8-bit length
    // field is modeled, as Zephyr always selects it.
    //
    // The same peripheral is the address resolver (AAR), which a controller with privacy uses to
    // recognize a bonded device's private address: ENABLE 3 selects it, and its registers sit where
    // the CCM's are. TASKS_START is TASKS_KSGEN; END, RESOLVED and NOTRESOLVED are ENDKSGEN,
    // ENDCRYPT and ERROR; NIRK shares MODE's register, IRKPTR CNFPTR's, ADDRPTR OUTPTR's, and STATUS
    // MICSTATUS's.
    public class NRF52840_CCM : BasicDoubleWordPeripheral, IKnownSize
    {
        public NRF52840_CCM(IMachine machine) : base(machine)
        {
            IRQ = new GPIO();
            DefineRegisters();
        }

        public override void Reset()
        {
            base.Reset();
            IRQ.Unset();
        }

        public long Size => 0x1000;

        public GPIO IRQ { get; }

        private void DefineRegisters()
        {
            Registers.GenerateKeyStream.Define(this)
                .WithFlag(0, FieldMode.Write, writeCallback: (_, value) =>
                {
                    if(!value)
                    {
                        return;
                    }
                    if(enable.Value == AddressResolverEnabled)
                    {
                        Resolve();
                        return;
                    }
                    keyStreamEnded.Value = true;
                    UpdateInterrupt();
                    if(cryptAfterKeyStream.Value)
                    {
                        Crypt();
                    }
                }, name: "TASKS_KSGEN, AAR TASKS_START")
                .WithReservedBits(1, 31);

            Registers.Crypt.Define(this)
                .WithFlag(0, FieldMode.Write, writeCallback: (_, value) =>
                {
                    if(value)
                    {
                        Crypt();
                    }
                }, name: "TASKS_CRYPT")
                .WithReservedBits(1, 31);

            Registers.KeyStreamEnded.Define(this)
                .WithFlag(0, out keyStreamEnded, name: "EVENTS_ENDKSGEN, AAR EVENTS_END")
                .WithReservedBits(1, 31)
                .WithWriteCallback((_, __) => UpdateInterrupt());

            Registers.CryptEnded.Define(this)
                .WithFlag(0, out cryptEnded, name: "EVENTS_ENDCRYPT, AAR EVENTS_RESOLVED")
                .WithReservedBits(1, 31)
                .WithWriteCallback((_, __) => UpdateInterrupt());

            Registers.Error.Define(this)
                .WithFlag(0, out error, name: "EVENTS_ERROR, AAR EVENTS_NOTRESOLVED")
                .WithReservedBits(1, 31)
                .WithWriteCallback((_, __) => UpdateInterrupt());

            Registers.Shortcuts.Define(this)
                .WithFlag(0, out cryptAfterKeyStream, name: "ENDKSGEN_CRYPT")
                .WithReservedBits(1, 31);

            Registers.InterruptEnableSet.Define(this)
                .WithFlag(0, out keyStreamInterrupt, FieldMode.Set | FieldMode.Read,
                    name: "ENDKSGEN")
                .WithFlag(1, out cryptInterrupt, FieldMode.Set | FieldMode.Read, name: "ENDCRYPT")
                .WithFlag(2, out errorInterrupt, FieldMode.Set | FieldMode.Read, name: "ERROR")
                .WithReservedBits(3, 29)
                .WithWriteCallback((_, __) => UpdateInterrupt());

            Registers.InterruptEnableClear.Define(this)
                .WithFlag(0, writeCallback: (_, value) => keyStreamInterrupt.Value &= !value,
                    valueProviderCallback: _ => keyStreamInterrupt.Value, name: "ENDKSGEN")
                .WithFlag(1, writeCallback: (_, value) => cryptInterrupt.Value &= !value,
                    valueProviderCallback: _ => cryptInterrupt.Value, name: "ENDCRYPT")
                .WithFlag(2, writeCallback: (_, value) => errorInterrupt.Value &= !value,
                    valueProviderCallback: _ => errorInterrupt.Value, name: "ERROR")
                .WithReservedBits(3, 29)
                .WithWriteCallback((_, __) => UpdateInterrupt());

            // The CCM's MIC check, 1 if it passed, or the index of the key the AAR resolved with.
            Registers.MicStatus.Define(this)
                .WithValueField(0, 4, out status, FieldMode.Read, name: "MICSTATUS, AAR STATUS")
                .WithReservedBits(4, 28);

            Registers.Enable.Define(this)
                .WithValueField(0, 2, out enable, name: "ENABLE")
                .WithReservedBits(2, 30);

            // For the CCM, bit 0 is MODE, 1 to decrypt; for the AAR, the field is NIRK, the number
            // of keys.
            Registers.Mode.Define(this, 0x1)
                .WithValueField(0, 5, out modeOrKeyCount, name: "MODE, AAR NIRK")
                .WithReservedBits(5, 11)
                .WithValueField(16, 2, name: "DATARATE")
                .WithReservedBits(18, 6)
                .WithFlag(24, name: "LENGTH")
                .WithReservedBits(25, 7);

            Registers.ConfigurationPointer.Define(this)
                .WithValueField(0, 32, out configurationPointer, name: "CNFPTR");

            Registers.InputPointer.Define(this)
                .WithValueField(0, 32, out inputPointer, name: "INPTR");

            Registers.OutputPointer.Define(this)
                .WithValueField(0, 32, out outputPointer, name: "OUTPTR");

            Registers.ScratchPointer.Define(this)
                .WithValueField(0, 32, name: "SCRATCHPTR");

            Registers.MaxPacketSize.Define(this, 0xFB)
                .WithValueField(0, 8, name: "MAXPACKETSIZE")
                .WithReservedBits(8, 24);
        }

        private void Crypt()
        {
            CryptPacket();
            cryptEnded.Value = true;
            UpdateInterrupt();
        }

        private void CryptPacket()
        {
            var configuration = sysbus.ReadBytes(configurationPointer.Value, ConfigurationSize);
            var header = sysbus.ReadBytes(inputPointer.Value, HeaderSize);
            // Empty packets, which keep a connection going, are sent without encryption or a MIC.
            if(header[LengthIndex] == 0)
            {
                sysbus.WriteBytes(header, outputPointer.Value);
                return;
            }
            var data = sysbus.ReadBytes(inputPointer.Value + HeaderSize, header[LengthIndex]);
            var key = new byte[KeySize];
            Array.Copy(configuration, key, KeySize);

            using(var ccm = new BluetoothCcm(key, Nonce(configuration)))
            {
                byte[] result;
                if((modeOrKeyCount.Value & 1) != 0)
                {
                    result = ccm.Decrypt(header[0], data, out var valid);
                    status.Value = valid ? 1u : 0u;
                }
                else
                {
                    result = ccm.Encrypt(header[0], data);
                }
                header[LengthIndex] = (byte)result.Length;
                sysbus.WriteBytes(header, outputPointer.Value);
                sysbus.WriteBytes(result, outputPointer.Value + HeaderSize);
            }
        }

        // Whether the address where a packet's payload starts, ADDRPTR + 3, is a private address one
        // of the NIRK keys at IRKPTR resolves: its hash, the low 3 bytes, must be ah(key, prand) of
        // the top 3 (Core specification Vol 3, Part H, 2.2.2). Keys are 16 bytes, most significant
        // byte first; the address is least significant byte first, as a packet carries it.
        private void Resolve()
        {
            var address = sysbus.ReadBytes(outputPointer.Value + AddressOffset, AddressSize);
            for(var index = 0; index < (int)modeOrKeyCount.Value; index++)
            {
                var key = sysbus.ReadBytes(configurationPointer.Value + (ulong)(index * KeySize), KeySize);
                if(Hash(key, address).SequenceEqual(address.Take(HashSize)))
                {
                    status.Value = (ulong)index;
                    cryptEnded.Value = true;
                    keyStreamEnded.Value = true;
                    UpdateInterrupt();
                    return;
                }
            }
            error.Value = true;
            keyStreamEnded.Value = true;
            UpdateInterrupt();
        }

        // ah: the low 3 bytes of the key's AES encryption of prand, zero-padded in front. Returned
        // least significant byte first, as the address holds its hash.
        private static byte[] Hash(byte[] key, byte[] address)
        {
            var block = new byte[AesBlockSize];
            for(var i = 0; i < HashSize; i++)
            {
                block[AesBlockSize - 1 - i] = address[HashSize + i];
            }
            using(var aes = AesProvider.GetEcbProvider(key))
            {
                aes.EncryptBlockInSitu(Block.UsingBytes(block));
            }
            return Enumerable.Range(1, HashSize).Select(i => block[AesBlockSize - i]).ToArray();
        }

        private void UpdateInterrupt()
        {
            IRQ.Set((keyStreamEnded.Value && keyStreamInterrupt.Value)
                || (cryptEnded.Value && cryptInterrupt.Value)
                || (error.Value && errorInterrupt.Value));
        }

        // The configuration holds the 39-bit packet counter least significant byte first, then the
        // direction bit (1 for packets from the central) and the IV. The nonce is the counter's
        // first 5 bytes with the direction bit on top, then the IV.
        private static byte[] Nonce(byte[] configuration)
        {
            var nonce = new byte[BluetoothCcm.NonceSize];
            Array.Copy(configuration, CounterOffset, nonce, 0, CounterSize);
            var direction = configuration[DirectionOffset] & 1;
            nonce[CounterSize - 1] = (byte)((nonce[CounterSize - 1] & 0x7F) | (direction << 7));
            Array.Copy(configuration, IvOffset, nonce, CounterSize, IvSize);
            return nonce;
        }

        private IFlagRegisterField keyStreamEnded;
        private IFlagRegisterField cryptEnded;
        private IFlagRegisterField error;
        private IFlagRegisterField keyStreamInterrupt;
        private IFlagRegisterField cryptInterrupt;
        private IFlagRegisterField errorInterrupt;
        private IFlagRegisterField cryptAfterKeyStream;
        private IValueRegisterField status;
        private IValueRegisterField enable;
        private IValueRegisterField modeOrKeyCount;
        private IValueRegisterField configurationPointer;
        private IValueRegisterField inputPointer;
        private IValueRegisterField outputPointer;

        private const int KeySize = 16;
        private const int CounterOffset = 16;
        private const int CounterSize = 5;
        private const int DirectionOffset = 24;
        private const int IvOffset = 25;
        private const int IvSize = 8;
        private const int ConfigurationSize = IvOffset + IvSize;
        private const int HeaderSize = 3;
        private const int LengthIndex = 1;
        private const int AddressResolverEnabled = 3;
        private const int AddressOffset = 3;
        private const int AddressSize = 6;
        private const int HashSize = 3;
        private const int AesBlockSize = 16;

        private enum Registers : long
        {
            GenerateKeyStream = 0x000,
            Crypt = 0x004,
            KeyStreamEnded = 0x100,
            CryptEnded = 0x104,
            Error = 0x108,
            Shortcuts = 0x200,
            InterruptEnableSet = 0x304,
            InterruptEnableClear = 0x308,
            MicStatus = 0x400,
            Enable = 0x500,
            Mode = 0x504,
            ConfigurationPointer = 0x508,
            InputPointer = 0x50C,
            OutputPointer = 0x510,
            ScratchPointer = 0x514,
            MaxPacketSize = 0x518,
        }

        // AES-CCM as Bluetooth uses it (Core specification Vol 6, Part E, 1): a 13-byte nonce, the
        // packet's header byte as associated data with SN, NESN and MD masked out, and a 4-byte
        // MIC.
        private sealed class BluetoothCcm : IDisposable
        {
            public BluetoothCcm(byte[] key, byte[] nonce)
            {
                aes = AesProvider.GetEcbProvider(key);
                this.nonce = nonce;
            }

            public void Dispose()
            {
                aes.Dispose();
            }

            // The encrypted payload followed by its MIC.
            public byte[] Encrypt(byte header, byte[] payload)
            {
                var mic = Mic(header, payload);
                var encrypted = new byte[payload.Length + MicSize];
                Array.Copy(CounterMode(payload), encrypted, payload.Length);
                Array.Copy(mic, 0, encrypted, payload.Length, MicSize);
                return encrypted;
            }

            // The payload, from the encrypted payload and its MIC. A packet too short to hold a MIC
            // can't be authenticated.
            public byte[] Decrypt(byte header, byte[] encrypted, out bool micValid)
            {
                if(encrypted.Length < MicSize)
                {
                    micValid = false;
                    return new byte[0];
                }
                var payloadSize = encrypted.Length - MicSize;
                var payload = CounterMode(encrypted.Take(payloadSize).ToArray());
                micValid = Mic(header, payload).SequenceEqual(encrypted.Skip(payloadSize));
                return payload;
            }

            // CBC-MAC over the first block (flags, nonce, payload length), the associated data
            // (its 2-byte length, then the masked header) and the payload, encrypted with counter
            // block 0.
            private byte[] Mic(byte header, byte[] payload)
            {
                var first = new byte[BlockSize];
                first[0] = MacFlags;
                Array.Copy(nonce, 0, first, 1, NonceSize);
                first[BlockSize - 2] = (byte)(payload.Length >> 8);
                first[BlockSize - 1] = (byte)payload.Length;
                var mac = EncryptBlock(first);

                var associatedData = new byte[BlockSize];
                associatedData[1] = 1;
                associatedData[2] = (byte)(header & HeaderMask);
                mac = EncryptBlock(Xor(mac, associatedData));

                for(var offset = 0; offset < payload.Length; offset += BlockSize)
                {
                    var block = new byte[BlockSize];
                    var count = Math.Min(BlockSize, payload.Length - offset);
                    Array.Copy(payload, offset, block, 0, count);
                    mac = EncryptBlock(Xor(mac, block));
                }

                var mic = Xor(mac, EncryptBlock(CounterBlock(0)));
                Array.Resize(ref mic, MicSize);
                return mic;
            }

            // Encryption and decryption alike: the data XORed with counter blocks 1, 2, ...
            private byte[] CounterMode(byte[] data)
            {
                var result = new byte[data.Length];
                for(var offset = 0; offset < data.Length; offset += BlockSize)
                {
                    var keyStream = EncryptBlock(CounterBlock(offset / BlockSize + 1));
                    for(var i = offset; i < Math.Min(offset + BlockSize, data.Length); i++)
                    {
                        result[i] = (byte)(data[i] ^ keyStream[i - offset]);
                    }
                }
                return result;
            }

            private byte[] CounterBlock(int index)
            {
                var block = new byte[BlockSize];
                block[0] = CounterFlags;
                Array.Copy(nonce, 0, block, 1, NonceSize);
                block[BlockSize - 2] = (byte)(index >> 8);
                block[BlockSize - 1] = (byte)index;
                return block;
            }

            private byte[] EncryptBlock(byte[] block)
            {
                var result = (byte[])block.Clone();
                aes.EncryptBlockInSitu(Block.UsingBytes(result));
                return result;
            }

            private static byte[] Xor(byte[] a, byte[] b)
            {
                var result = new byte[a.Length];
                for(var i = 0; i < a.Length; i++)
                {
                    result[i] = (byte)(a[i] ^ b[i]);
                }
                return result;
            }

            public const int NonceSize = 13;

            private readonly AesProvider aes;
            private readonly byte[] nonce;

            private const int BlockSize = 16;
            private const int MicSize = 4;
            // RFC 3610's flags: associated data present, a 4-byte MIC and a 2-byte length field;
            // and for the counter blocks, the 2-byte counter.
            private const byte MacFlags = 0x49;
            private const byte CounterFlags = 0x01;
            private const byte HeaderMask = 0xE3;
        }
    }
}
