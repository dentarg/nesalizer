#include "common.h"

#include "cpu.h"
#include "mapper.h"
#include "rom.h"

static uint8_t nop_read(uint16_t) { return cpu_data_bus; } // Return open bus by default
static void    nop_write(uint8_t, uint16_t) {}
static void    nop_ppu_tick_callback() {}
static uint8_t bad_nt_read(uint16_t addr) {
    fail("internal error: reading nametable address %04X with no read function defined",
         addr);
}
static void    bad_nt_write(uint8_t val, uint16_t addr) {
    fail("internal error: writing %02X to nametable address %04X with no write function defined",
         val, addr);
}
// For stateless mappers
static size_t nop_state_fn(uint8_t*&) { return 0; }

// Implicitly zero-initialized
Mapper_fns mapper_functions[256];

read_fn              *read_mapper;
write_fn             *write_mapper;
ppu_tick_callback_fn *ppu_tick_callback;
read_nt_fn           *mapper_read_nt;
write_nt_fn          *mapper_write_nt;
state_fn             *mapper_state_size;
state_fn             *mapper_save_state;
state_fn             *mapper_load_state;

// Workaround for not being able to declare templates inside functions
#define DECLARE_STATE_FNS(n)              \
  template<bool, bool> size_t             \
  transfer_mapper_##n##_state(uint8_t*&);
DECLARE_STATE_FNS(  1) DECLARE_STATE_FNS(   2) DECLARE_STATE_FNS(  3) DECLARE_STATE_FNS(  4)
DECLARE_STATE_FNS(  5) DECLARE_STATE_FNS(   7) DECLARE_STATE_FNS(  9) DECLARE_STATE_FNS( 11)
DECLARE_STATE_FNS( 71) DECLARE_STATE_FNS( 232)

void init_mappers() {
    // Helper for initializing state saving/loading functions
    #define MAPPER_STATE_FNS(n)                                                   \
      mapper_functions[n].state_size = transfer_mapper_##n##_state<true, false>;  \
      mapper_functions[n].save_state = transfer_mapper_##n##_state<false, true>;  \
      mapper_functions[n].load_state = transfer_mapper_##n##_state<false, false>;

    // No mapper (hardwired/NROM)
    #define MAPPER_NONE(n)                                           \
      void mapper_##n##_init();                                      \
      mapper_functions[n].init              = mapper_##n##_init;     \
      mapper_functions[n].read              = nop_read;              \
      mapper_functions[n].write             = nop_write;             \
      mapper_functions[n].ppu_tick_callback = nop_ppu_tick_callback; \
      mapper_functions[n].read_nt           = bad_nt_read;           \
      mapper_functions[n].write_nt          = bad_nt_write;          \
      mapper_functions[n].state_size        = nop_state_fn;          \
      mapper_functions[n].save_state        = nop_state_fn;          \
      mapper_functions[n].load_state        = nop_state_fn;

    // Mapper that only reacts to writes
    #define MAPPER_W(n)                                              \
      void mapper_##n##_init();                                      \
      void mapper_##n##_write(uint8_t, uint16_t);                    \
      mapper_functions[n].init              = mapper_##n##_init;     \
      mapper_functions[n].read              = nop_read;              \
      mapper_functions[n].write             = mapper_##n##_write;    \
      mapper_functions[n].ppu_tick_callback = nop_ppu_tick_callback; \
      mapper_functions[n].read_nt           = bad_nt_read;           \
      mapper_functions[n].write_nt          = bad_nt_write;          \
      MAPPER_STATE_FNS(n)

    // Mapper that reacts to writes and (P)PU events
    #define MAPPER_WP(n)                                                      \
      void mapper_##n##_init();                                               \
      void mapper_##n##_write(uint8_t, uint16_t);                             \
      void mapper_##n##_ppu_tick_callback();                                  \
      mapper_functions[n].init              = mapper_##n##_init;              \
      mapper_functions[n].read              = nop_read;                       \
      mapper_functions[n].write             = mapper_##n##_write;             \
      mapper_functions[n].ppu_tick_callback = mapper_##n##_ppu_tick_callback; \
      mapper_functions[n].read_nt           = bad_nt_read;                    \
      mapper_functions[n].write_nt          = bad_nt_write;                   \
      MAPPER_STATE_FNS(n)

    // Mapper that reacts to reads, writes, PPU events, and has special
    // (n)ametable mirroring (e.g. MMC5)
    #define MAPPER_RWPN(n)                                                    \
      void mapper_##n##_init();                                               \
      uint8_t mapper_##n##_read(uint16_t);                                    \
      void mapper_##n##_write(uint8_t, uint16_t);                             \
      void mapper_##n##_ppu_tick_callback();                                  \
      uint8_t mapper_##n##_read_nt(uint16_t);                                 \
      void mapper_##n##_write_nt(uint8_t, uint16_t);                          \
      mapper_functions[n].init              = mapper_##n##_init;              \
      mapper_functions[n].read              = mapper_##n##_read;              \
      mapper_functions[n].write             = mapper_##n##_write;             \
      mapper_functions[n].ppu_tick_callback = mapper_##n##_ppu_tick_callback; \
      mapper_functions[n].read_nt           = mapper_##n##_read_nt;           \
      mapper_functions[n].write_nt          = mapper_##n##_write_nt;          \
      MAPPER_STATE_FNS(n)

    // NROM
    MAPPER_NONE(     0)
    // SxROM, all of which use the Nintendo MMC1
    MAPPER_W(        1)
    // Most common configuration of the UxROM boardset
    MAPPER_W(        2)
    // CNROM board and a very similar board used for Panesian games
    MAPPER_W(        3)
    // "iNES Mapper 004 is a wide abstraction that can represent boards using the
    // Nintendo MMC3, Nintendo MMC6, or functional clones of any of the above. Most
    // games utilizing TxROM, DxROM, and HKROM boards use this designation."
    MAPPER_WP(       4)
    // MMC5/ExROM - Used by Castlevania III
    MAPPER_RWPN(     5)
    // AxROM - Rare games often use this one
    MAPPER_W(        7)
    // MMC2 - only used by Punch-Out!!
    MAPPER_WP(       9)
    // Color Dreams
    MAPPER_W(       11)
    // Mapper-2-ish
    MAPPER_W(       71)
    // Camerica/Capcom mapper used by the Quattro * games
    MAPPER_W(      232)

    #undef MAPPER_STATE_FNS
    #undef MAPPER_NONE
    #undef MAPPER_W
    #undef MAPPER_WP
    #undef MAPPER_RWPN
}

//
// Memory mapping
//

// Each PRG page is 8 KB to account for the finest granularity switched by any
// mapper
uint8_t *prg_pages      [4];
bool     prg_page_is_ram[4]; // MMC5 can map PRG RAM into the $8000+ range
// 8 KB page mapped at $6000-$7FFF. MMC5 can remap this.
uint8_t *prg_ram_6000_page;

// Each 1 KB big
uint8_t *chr_pages[8];

// Memory remapping functions. 'n' specifies the slot, 'bank' the bank to map
// there. Both are in units corresponding to the function.
//
// Negative bank numbers to set_prg_16/8k_bank() assign banks from the end, so
// that e.g. set_prg_16k_bank(0, -2) assigns the second-to-last 16 KB bank to
// the first 16 KB slot.

void set_prg_32k_bank(unsigned bank) {
    if (prg_16k_banks == 1) {
        // The only configuration for a single 16k PRG bank is to be mirrored
        // in $8000-$BFFF and $C000-$FFFF
        prg_pages[0] = prg_pages[2] = prg_base;
        prg_pages[1] = prg_pages[3] = prg_base + 0x2000;
    }
    else {
        uint8_t *const bank_ptr = prg_base + 0x8000*(bank & (prg_16k_banks/2 - 1));
        for (unsigned i = 0; i < 4; ++i) {
            prg_pages[i]       = bank_ptr + 0x2000*i;
            prg_page_is_ram[i] = false;
        }
    }
}

void set_prg_16k_bank(unsigned n, int bank, bool is_rom /* = true */) {
    assert(n < 2);

    if (bank < 0)
        bank = max(int(prg_16k_banks + bank), 0);

    uint8_t *base;
    unsigned mask;
    if (!is_rom && prg_ram_base) {
        base = prg_ram_base;
        mask = 2*prg_ram_8k_banks - 1;
    }
    else {
        base = prg_base;
        mask = prg_16k_banks - 1;
    }

    uint8_t *const bank_ptr = base + 0x4000*(bank & mask);
    for (unsigned i = 0; i < 2; ++i) {
        prg_pages[2*n + i] = bank_ptr + 0x2000*i;
        prg_page_is_ram[2*n + i] = !is_rom;
    }
}

void set_prg_8k_bank(unsigned n, int bank, bool is_rom /* = true */) {
    assert(n < 4);

    if (bank < 0)
        bank = max(int(2*prg_16k_banks + bank), 0);

    uint8_t *base;
    unsigned mask;
    if (!is_rom && prg_ram_base) {
        base = prg_ram_base;
        mask = prg_ram_8k_banks - 1;
    }
    else {
        base = prg_base;
        mask = 2*prg_16k_banks - 1;
    }

    prg_pages[n] = base + 0x2000*(bank & mask);
    prg_page_is_ram[n] = !is_rom;
}

void set_prg_6000_bank(unsigned bank) {
    prg_ram_6000_page = prg_ram_base + 0x2000*(bank & (prg_ram_8k_banks - 1));
}

void set_chr_8k_bank(unsigned bank) {
    uint8_t *const bank_ptr = chr_base + 0x2000*(bank & (chr_8k_banks - 1));
    for (unsigned i = 0; i < 8; ++i)
        chr_pages[i] = bank_ptr + 0x400*i;
}

void set_chr_4k_bank(unsigned n, unsigned bank) {
    assert(n < 2);
    uint8_t *const bank_ptr = chr_base + 0x1000*(bank & (2*chr_8k_banks - 1));
    for (unsigned i = 0; i < 4; ++i)
        chr_pages[4*n + i] = bank_ptr + 0x400*i;
}

void set_chr_2k_bank(unsigned n, unsigned bank) {
    assert(n < 4);
    uint8_t *const bank_ptr = chr_base + 0x800*(bank & (4*chr_8k_banks - 1));
    for (unsigned i = 0; i < 2; ++i)
        chr_pages[2*n + i] = bank_ptr + 0x400*i;
}

void set_chr_1k_bank(unsigned n, unsigned bank) {
    assert(n < 8);
    chr_pages[n] = chr_base + 0x400*(bank & (8*chr_8k_banks - 1));
}

//
// Mirroring
//

Mirroring mirroring;

void set_mirroring(Mirroring m) {
    // In four-screen mode, the cart is assumed to be wired so that the mapper
    // can't influence mirroring
    if (mirroring != FOUR_SCREEN)
        mirroring = m;
}
