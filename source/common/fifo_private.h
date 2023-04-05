// SPDX-License-Identifier: Zlib
//
// Copyright (c) 2008-2015 Dave Murphy (WinterMute)
// Copyright (c) 2023 Antonio Niño Díaz

#ifndef FIFO_PRIVATE_H__
#define FIFO_PRIVATE_H__

#include <stdbool.h>

#include <nds/ndstypes.h>

// Maximum number of bytes that can be sent in a fifo message
#define FIFO_MAX_DATA_BYTES     128

// Number of words that can be stored temporarily while waiting to deque them
#ifdef ARM9
#define FIFO_BUFFER_ENTRIES     256
#else // ARM7
#define FIFO_BUFFER_ENTRIES     256
#endif

// In the fifo_buffer[] array, this value means that there are no more values
// left to handle.
#define FIFO_BUFFER_TERMINATE   0xFFFF

// Mask used to extract the index in fifo_buffer[] of the next block
#define FIFO_BUFFER_NEXTMASK    0xFFFF

// The memory overhead of this library (per CPU) is:
//
//     16 + (NUM_CHANNELS * 32) + (FIFO_BUFFER_ENTRIES * 8)
//
// For 16 channels and 256 entries, this is 16 + 512 + 2048 = 2576 bytes of ram.
//
// Some padding may be added by the compiler, though.

// Defines related to the header block of a FIFO message
// -----------------------------------------------------

// Number of bits used to specify the channel of a packet
#define FIFO_CHANNEL_BITS       4

#define FIFO_NUM_CHANNELS       (1 << FIFO_CHANNEL_BITS)
#define FIFO_CHANNEL_SHIFT      (32 - FIFO_CHANNEL_BITS)
#define FIFO_CHANNEL_MASK       ((1 << FIFO_CHANNEL_BITS) - 1)

// If this bit is set, the message is an address (0x02000000 - 0x02FFFFFF)
#define FIFO_ADDRESSBIT_SHIFT   (FIFO_CHANNEL_SHIFT - 1)
#define FIFO_ADDRESSBIT         (1 << FIFO_ADDRESSBIT_SHIFT)

// If this bit is set, the message is an immediate value.
#define FIFO_IMMEDIATEBIT_SHIFT (FIFO_ADDRESSBIT_SHIFT - 1)
#define FIFO_IMMEDIATEBIT       (1 << FIFO_IMMEDIATEBIT_SHIFT)

// If this bit is set, it means that the provided immediate value doesn't fit in
// a 32-bit header block. In that case, the value is sent in the block right
// after the header.
#define FIFO_EXTRABIT_SHIFT     (FIFO_IMMEDIATEBIT_SHIFT - 1)
#define FIFO_EXTRABIT           (1 << FIFO_EXTRABIT_SHIFT)

// Note: Some special commands can be accessed by setting the address bit and
// the immediate bit at the same time. This isn't normally allowed. Also, if
// both bits are 0, this is a data message of an arbitrary length.

// 31 ... 28 |  27  | 26    | 25    | 24 ... 0        || 31 ... 0
// ----------+------+-------+-------+-----------------++-----------------
//  Channel  | Addr | Immed | Extra | Data            ||
// ----------+------+-------+-------+-----------------++-----------------
//
//  Messages of immediate values
//
//  Channel  |  0   |  1    |   0   | Small immediate ||
//  Channel  |  0   |  1    |   1   | X               || 32-bit immediate
//
//  Messages of addresses
//
//  Channel  |  1   |  0    |   X   | Address         ||
//
//  Messages of data of arbitrary size
//
//  Channel  |  0   |  0    |   X   | Length (bytes)  || Word 0 (first of many)
//
//  Messages of special commands (the channel is ignored)
//
//    X      |  1   |  1    |   X   | Command         ||

static inline uint32_t FIFO_UNPACK_CHANNEL(uint32_t dataword)
{
    return (dataword >> FIFO_CHANNEL_SHIFT) & FIFO_CHANNEL_MASK;
}

// Defines related to 32-bit immediate value messages
// --------------------------------------------------

#define FIFO_VALUE32_MASK   (FIFO_EXTRABIT - 1)

// This returns true if the block is an immediate value (with extra word or not)
static inline bool FIFO_IS_VALUE32(uint32_t dataword)
{
    return ((dataword & FIFO_ADDRESSBIT) == 0) &&
           ((dataword & FIFO_IMMEDIATEBIT) != 0);
}

// This returns true if the 32-bit value doesn't fit in one FIFO block. In that
// case, it needs an extra FIFO block.
static inline bool FIFO_VALUE32_NEEDEXTRA(uint32_t value32)
{
    return (value32 & ~FIFO_VALUE32_MASK) != 0;
}

// Returns true if the specified fifo block says it needs an extra word.
static inline bool FIFO_UNPACK_VALUE32_NEEDEXTRA(uint32_t dataword)
{
    return (dataword & FIFO_EXTRABIT) != 0;
}

// This creates a FIFO message that sends a 32-bit value that fits in one block.
static inline uint32_t FIFO_PACK_VALUE32(uint32_t channel, uint32_t value32)
{
    return (channel << FIFO_CHANNEL_SHIFT) | FIFO_IMMEDIATEBIT |
            (value32 & FIFO_VALUE32_MASK);
}

// Extract the small immediate value in messages that don't need an extra word.
static inline uint32_t FIFO_UNPACK_VALUE32_NOEXTRA(uint32_t dataword)
{
    return dataword & FIFO_VALUE32_MASK;
}

// This creates the header of a FIFO message that sends a 32-bit value that
// doesn't fits in one block.
static inline uint32_t FIFO_PACK_VALUE32_EXTRA(uint32_t channel)
{
    return (channel << FIFO_CHANNEL_SHIFT) | FIFO_IMMEDIATEBIT | FIFO_EXTRABIT;
}

// Defines related to address messages
// -----------------------------------

#define FIFO_ADDRESSDATA_SHIFT          0
#define FIFO_MINADDRESSDATABITS         24
#define FIFO_ADDRESSDATA_MASK           0x00FFFFFF
#define FIFO_ADDRESSBASE                0x02000000
#define FIFO_ADDRESSCOMPATIBLE          0xFF000000

// This creates a FIFO message that sends an address in one FIFO block.
static inline uint32_t FIFO_PACK_ADDRESS(uint32_t channel, void *address)
{
    return (channel << FIFO_CHANNEL_SHIFT) | FIFO_ADDRESSBIT |
           (((uint32_t)address >> FIFO_ADDRESSDATA_SHIFT) & FIFO_ADDRESSDATA_MASK);
}

// This returns true if the address can be sent as a FIFO address message. It
// needs to be placed in main RAM for it to be compatible.
static inline bool FIFO_IS_ADDRESS_COMPATIBLE(void *address)
{
    return ((uint32_t)address & FIFO_ADDRESSCOMPATIBLE) == FIFO_ADDRESSBASE;
}

static inline bool FIFO_IS_ADDRESS(uint32_t dataword)
{
    return (dataword & FIFO_ADDRESSBIT) != 0;
}

static inline void *FIFO_UNPACK_ADDRESS(uint32_t dataword)
{
    uint32_t address = ((dataword & FIFO_ADDRESSDATA_MASK) << FIFO_ADDRESSDATA_SHIFT)
                     | FIFO_ADDRESSBASE;
    return (void *)address;
}

// Defines related to data messages
// --------------------------------

// This creates the header of a FIFO message that sends an arbitrary number of
// bytes. The actual bytes must be sent right after the header.
static inline uint32_t FIFO_PACK_DATAMSG_HEADER(uint32_t channel, uint32_t numbytes)
{
    return (channel << FIFO_CHANNEL_SHIFT) | (numbytes & FIFO_VALUE32_MASK);
}

static inline bool FIFO_IS_DATA(uint32_t dataword)
{
    return (dataword & (FIFO_ADDRESSBIT | FIFO_IMMEDIATEBIT)) == 0;
}

static inline uint32_t FIFO_UNPACK_DATALENGTH(uint32_t dataword)
{
	return dataword & FIFO_VALUE32_MASK;
}

// Defines related to special commands
// -----------------------------------

// This returns true if the block is a special command
static inline bool FIFO_IS_SPECIAL_COMMAND(uint32_t dataword)
{
    return ((dataword & FIFO_ADDRESSBIT) != 0) &&
           ((dataword & FIFO_IMMEDIATEBIT) != 0);
}

#define FIFO_SPECIAL_COMMAND_MASK       0x00FFFFFF

#define FIFO_ARM9_REQUESTS_ARM7_RESET   0x4000C
#define FIFO_ARM7_REQUESTS_ARM9_RESET   0x4000B

// ----------------------------------------------------------------------

bool fifoInternalSend(u32 firstword, u32 extrawordcount, u32 *wordlist);

#endif // FIFO_PRIVATE_H__
