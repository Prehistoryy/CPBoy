/**
 * MIT License
 *
 * Copyright (c) 2018 Mahyar Koshkouei
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <stdint.h> /* Required for int types */
#include <string.h>	/* Required for memset() */
#include <time.h>

/* Enable sound support, including sound registers. Off by default due to no
 * implementation. */
#ifndef ENABLE_SOUND
#define ENABLE_SOUND 0
#endif

/* Enable LCD drawing. On by default. May be turned off for testing purposes. */
#ifndef ENABLE_LCD
#define ENABLE_LCD 1
#endif

/* Interrupt masks */
#define VBLANK_INTR		0x01
#define LCDC_INTR		0x02
#define TIMER_INTR		0x04
#define SERIAL_INTR		0x08
#define CONTROL_INTR	0x10
#define ANY_INTR		0x1F

/* Memory section sizes for DMG */
#define WRAM_SIZE		0x2000
#define VRAM_SIZE		0x2000
#define HRAM_SIZE		0x0100
#define OAM_SIZE		0x00A0

/* Memory addresses */
#define ROM_0_ADDR      0x0000
#define ROM_N_ADDR      0x4000
#define VRAM_ADDR       0x8000
#define CART_RAM_ADDR   0xA000
#define WRAM_0_ADDR     0xC000
#define WRAM_1_ADDR     0xD000
#define ECHO_ADDR       0xE000
#define OAM_ADDR        0xFE00
#define UNUSED_ADDR     0xFEA0
#define IO_ADDR         0xFF00
#define HRAM_ADDR       0xFF80
#define INTR_EN_ADDR    0xFFFF

/* Cart section sizes */
#define ROM_BANK_SIZE   0x4000
#define WRAM_BANK_SIZE  0x1000
#define CRAM_BANK_SIZE  0x2000
#define VRAM_BANK_SIZE  0x2000

/* TAC masks */
#define TAC_ENABLE          0x04
#define TAC_RATE            0x03

/* DIV Register is incremented at rate of 16384Hz.
 * 4194304 / 16384 = 256 clock cycles for one increment. */
#define DIV_CYCLES          256

/* STAT register masks */
#define STAT_LYC_INTR       0x40
#define STAT_MODE_2_INTR    0x20
#define STAT_MODE_1_INTR    0x10
#define STAT_MODE_0_INTR    0x08
#define STAT_LYC_COINC      0x04
#define STAT_MODE           0x03
#define STAT_USER_BITS      0xF8

/* LCDC control masks */
#define LCDC_ENABLE         0x80
#define LCDC_WINDOW_MAP     0x40
#define LCDC_WINDOW_ENABLE  0x20
#define LCDC_TILE_SELECT    0x10
#define LCDC_BG_MAP         0x08
#define LCDC_OBJ_SIZE       0x04
#define LCDC_OBJ_ENABLE     0x02
#define LCDC_BG_ENABLE      0x01

/* LCD characteristics */
#define LCD_LINE_CYCLES     456
#define LCD_MODE_0_CYCLES   0
#define LCD_MODE_2_CYCLES   204
#define LCD_MODE_3_CYCLES   284
#define LCD_VERT_LINES      154
#define LCD_WIDTH           160
#define LCD_HEIGHT          144

/* VRAM Locations */
#define VRAM_TILES_1        (0x8000 - VRAM_ADDR)
#define VRAM_TILES_2        (0x8800 - VRAM_ADDR)
#define VRAM_BMAP_1         (0x9800 - VRAM_ADDR)
#define VRAM_BMAP_2         (0x9C00 - VRAM_ADDR)
#define VRAM_TILES_3        (0x8000 - VRAM_ADDR + VRAM_BANK_SIZE)
#define VRAM_TILES_4        (0x8800 - VRAM_ADDR + VRAM_BANK_SIZE)

/* Interrupt jump addresses */
#define VBLANK_INTR_ADDR    0x0040
#define LCDC_INTR_ADDR      0x0048
#define TIMER_INTR_ADDR     0x0050
#define SERIAL_INTR_ADDR    0x0058
#define CONTROL_INTR_ADDR   0x0060

/* SPRITE controls */
#define NUM_SPRITES         0x28
#define MAX_SPRITES_LINE    0x0A
#define OBJ_PRIORITY        0x80
#define OBJ_FLIP_Y          0x40
#define OBJ_FLIP_X          0x20
#define OBJ_PALETTE         0x10

#ifndef MIN
#define MIN(a, b)   ((a) < (b) ? (a) : (b))
#endif

struct cpu_registers_t
{
	struct {
		/* Combine A and F registers. */
		union {
			struct {
				/* Define specific bits of Flag register. */
				union {
					struct {
						uint8_t unused : 4;
						uint8_t c : 1; /* Carry flag. */
						uint8_t h : 1; /* Half carry flag. */
						uint8_t n : 1; /* Add/sub flag. */
						uint8_t z : 1; /* Zero flag. */
					} f_bits;
					uint8_t f;
				};
				uint8_t a;
			};
			uint16_t af;
		};
	};

	struct {
		union {
			struct {
				uint8_t c;
				uint8_t b;
			};
			uint16_t bc;
		};
	};

	struct {
		union {
			struct {
				uint8_t e;
				uint8_t d;
			};
			uint16_t de;
		};
	};

	struct {
		union {
			struct {
				uint8_t l;
				uint8_t h;
			};
			uint16_t hl;
		};
	};

	uint16_t sp; /* Stack pointer */
	uint16_t pc; /* Program counter */
};

struct timer_t
{
	uint16_t cpu_count; /* Unused currently */
	uint16_t lcd_count;		/* LCD Timing */
	uint16_t div_count;		/* Divider Register Counter */
	uint16_t tima_count;	/* Timer Counter */
	uint8_t tac_enable;		/* Timer enable */
	uint8_t tac_rate;		/* Timer rate */
};

struct gb_registers_t
{
	/* TODO: Sort variables in address order. */
	/* Timing */
	uint8_t TIMA;	uint8_t TMA;	uint8_t TAC;	uint8_t DIV;

#if ENABLE_SOUND
	/* Sound */
	uint8_t NR10;	uint8_t NR11;	uint8_t NR12;	uint8_t NR13;
	uint8_t NR14;
	uint8_t NR21;	uint8_t NR22;	uint8_t NR23;	uint8_t NR24;
	uint8_t NR30;	uint8_t NR31;	uint8_t NR32;	uint8_t NR33;
	uint8_t NR34;
	uint8_t NR41;	uint8_t NR42;	uint8_t NR43;	uint8_t NR44;
	uint8_t NR50;	uint8_t NR51;	uint8_t NR52;
	uint8_t WAV[0x10];
#endif
	/* LCD */
	uint8_t LCDC;	uint8_t STAT;	uint8_t SCY;	uint8_t SCX;
	uint8_t LY;		uint8_t LYC;	uint8_t DMA;	uint8_t BGP;
	uint8_t OBP0;	uint8_t OBP1;	uint8_t WY;		uint8_t WX;

	/* Joypad info. */
	uint8_t P1;

	/* Serial data. */
	uint8_t SB;		uint8_t SC;

	/* Interrupt flag. */
	uint8_t IF;

	/* Interrupt enable. */
	uint8_t IE;
};

/**
 * Errors that may occur during emulation.
 */
enum gb_error_e
{
	GB_UNKNOWN_ERROR,
	GB_INVALID_OPCODE,
	GB_INVALID_READ,
	GB_INVALID_WRITE
};

/**
 * Errors that may occur during library initialisation.
 */
enum gb_init_error_e
{
	GB_INIT_NO_ERROR,
	GB_INIT_CARTRIDGE_UNSUPPORTED
};

struct gb_t
{
	uint8_t (*gb_rom_read)(struct gb_t*, const uint32_t);
	uint8_t (*gb_cart_ram_read)(struct gb_t*, const uint32_t);
	void (*gb_cart_ram_write)(struct gb_t*, const uint32_t,
			const uint8_t val);
	void (*gb_error)(struct gb_t*, const enum gb_error_e, const uint16_t);
	void *priv;

	struct
	{
		unsigned int	gb_halt : 1;
		unsigned int	gb_ime : 1;
		unsigned int	gb_bios_enable : 1;
		unsigned int	gb_frame : 1; /* New frame drawn. */
		enum
		{
			LCD_HBLANK = 0,
			LCD_VBLANK = 1,
			LCD_SEARCH_OAM = 2,
			LCD_TRANSFER = 3
		}				lcd_mode : 2;
	};

	/* Cartridge information:
	 * Memory Bank Controller (MBC) type. */
	uint8_t mbc;
	/* Whether the MBC has internal RAM. */
	uint8_t cart_ram;
	/* Number of ROM banks in cartridge. */
	uint16_t num_rom_banks;
	/* Number of RAM banks in cartridge. */
	uint8_t num_ram_banks;

	uint8_t selected_rom_bank;
	/* WRAM and VRAM bank selection not available. */
	uint8_t cart_ram_bank;
	uint8_t enable_cart_ram;
	/* Cartridge ROM/RAM mode select. */
	uint8_t cart_mode_select;
	union
	{
		struct
		{
			uint8_t sec;
			uint8_t min;
			uint8_t hour;
			uint8_t yday;
			uint8_t high;
		} rtc_bits;
		uint8_t cart_rtc[5];
	};

	struct cpu_registers_t cpu_reg;
	struct timer_t timer;
	struct gb_registers_t gb_reg;

	struct
	{
		union
		{
			struct
			{
				unsigned int a		: 1;
				unsigned int b		: 1;
				unsigned int select	: 1;
				unsigned int start	: 1;
				unsigned int right	: 1;
				unsigned int left	: 1;
				unsigned int up		: 1;
				unsigned int down	: 1;
			} joypad_bits;
			uint8_t joypad;
		};
	};

	/* TODO: Allow implementation to allocate WRAM, VRAM and Frame Buffer. */
	uint8_t wram[WRAM_SIZE];
	uint8_t vram[VRAM_SIZE];
	uint8_t hram[HRAM_SIZE];
	uint8_t oam[OAM_SIZE];

	/* Palettes */
	uint8_t BGP[4];
	uint8_t OBJP[8];

	/* screen */
	uint8_t gb_fb[LCD_HEIGHT][LCD_WIDTH];

	/* TODO: Move this */
	uint8_t WY;
	uint8_t WYC;
};

void gb_process_joypad(struct gb_t *gb)
{
	gb->gb_reg.P1 |= 0x0F;

	/* TODO: Complete joypad states. */

	/* Direction keys selected */
	if((gb->gb_reg.P1 & 0b010000) == 0)
		gb->gb_reg.P1 &= 0xF0 | (gb->joypad >> 4);
	/* Button keys selected */
	else if((gb->gb_reg.P1 & 0b100000) == 0)
		gb->gb_reg.P1 &= 0xF0 | (gb->joypad & 0x0F);
}

/**
 * Tick the internal RTC by one second.
 */
void gb_tick_rtc(struct gb_t *gb)
{
	/* is timer running? */
	if((gb->cart_rtc[4] & 0x40) == 0)
	{
		if(++gb->rtc_bits.sec == 60)
		{
			gb->rtc_bits.sec = 0;
			if(++gb->rtc_bits.min == 60)
			{
				gb->rtc_bits.min = 0;
				if(++gb->rtc_bits.hour == 24)
				{
					gb->rtc_bits.hour = 0;
					if(++gb->rtc_bits.yday == 0)
					{
						if (gb->rtc_bits.high & 1) /* Bit 8 of days*/
						{
							gb->rtc_bits.high |= 0x80; /* Overflow bit */
						}
						gb->rtc_bits.high ^= 1;
					}
				}
			}
		}
	}
}

/**
 * Set initial values in RTC.
 */
void gb_set_rtc(struct gb_t *gb, const struct tm * const time)
{
	gb->cart_rtc[0] = time->tm_sec;
	gb->cart_rtc[1] = time->tm_min;
	gb->cart_rtc[2] = time->tm_hour;
	gb->cart_rtc[3] = time->tm_yday & 0xFF; /* Low 8 bits of day counter. */
	gb->cart_rtc[4] = time->tm_yday >> 8; /* High 1 bit of day counter. */
}

uint8_t __gb_read(struct gb_t *gb, const uint16_t addr)
{
	switch(addr >> 12)
	{
		case 0x0:
			/* TODO: BIOS support. */
		case 0x1:
		case 0x2:
		case 0x3:
			return gb->gb_rom_read(gb, addr);

		case 0x4:
		case 0x5:
		case 0x6:
		case 0x7:
			if(gb->mbc == 1 && gb->cart_mode_select)
				return gb->gb_rom_read(gb, addr + ((gb->selected_rom_bank & 0x1F) - 1) * ROM_BANK_SIZE);
			else
				return gb->gb_rom_read(gb, addr + (gb->selected_rom_bank - 1) * ROM_BANK_SIZE);

		case 0x8:
		case 0x9:
			return gb->vram[addr - VRAM_ADDR];

		case 0xA:
		case 0xB:
			if(gb->cart_ram && gb->enable_cart_ram)
			{
				if(gb->mbc == 3 && gb->cart_ram_bank >= 0x08)
					return gb->cart_rtc[gb->cart_ram_bank - 0x08];
				else if((gb->cart_mode_select || gb->mbc != 1) &&
						gb->cart_ram_bank < gb->num_ram_banks)
				{
					return gb->gb_cart_ram_read(gb, addr - CART_RAM_ADDR +
							(gb->cart_ram_bank * CRAM_BANK_SIZE));
				}
				else
					return gb->gb_cart_ram_read(gb, addr - CART_RAM_ADDR);
			}
			return 0;

		case 0xC:
			return gb->wram[addr - WRAM_0_ADDR];

		case 0xD:
			return gb->wram[addr - WRAM_1_ADDR + WRAM_BANK_SIZE];

		case 0xE:
			return gb->wram[addr - ECHO_ADDR];

		case 0xF:
			if(addr < OAM_ADDR)
				return gb->wram[addr - ECHO_ADDR];
			if(addr < UNUSED_ADDR)
				return gb->oam[addr - OAM_ADDR];
			/* Unusable memory area. Reading from this area returns 0.*/
			if(addr < IO_ADDR)
				return 0;
			/* HRAM */
			if(HRAM_ADDR <= addr && addr < INTR_EN_ADDR)
				return gb->hram[addr - HRAM_ADDR];
			/* Wave pattern RAM */
			if((addr & 0xFFF0) == 0xFF30)
			{
#if ENABLE_SOUND
				return gb->gb_reg.WAV[addr & 0xF];
#else
				return 1;
#endif
			}
			/* IO and Interrupts. */
			switch (addr & 0xFF)
			{
						   /* IO Registers */
				case 0x00: return 0xC0 | gb->gb_reg.P1;
				case 0x01: return gb->gb_reg.SB;
				case 0x02: return gb->gb_reg.SC;

						   /* Timer Registers */
				case 0x04: return gb->gb_reg.DIV;
				case 0x05: return gb->gb_reg.TIMA;
				case 0x06: return gb->gb_reg.TMA;
				case 0x07: return gb->gb_reg.TAC;

						   /* Interrupt Flag Register */
				case 0x0F: return gb->gb_reg.IF;

#if ENABLE_SOUND
						   /* Sound registers */
				case 0x10: return gb->gb_reg.NR10;
				case 0x11: return gb->gb_reg.NR11;
				case 0x12: return gb->gb_reg.NR12;
				case 0x13: return gb->gb_reg.NR13;
				case 0x14: return gb->gb_reg.NR14;
				case 0x16: return gb->gb_reg.NR21;
				case 0x17: return gb->gb_reg.NR22;
				case 0x18: return gb->gb_reg.NR23;
				case 0x19: return gb->gb_reg.NR24;
				case 0x1A: return gb->gb_reg.NR30;
				case 0x1B: return gb->gb_reg.NR31;
				case 0x1C: return gb->gb_reg.NR32;
				case 0x1D: return gb->gb_reg.NR33;
				case 0x1E: return gb->gb_reg.NR34;
				case 0x20: return gb->gb_reg.NR41;
				case 0x21: return gb->gb_reg.NR42;
				case 0x22: return gb->gb_reg.NR43;
				case 0x23: return gb->gb_reg.NR44;
				case 0x24: return gb->gb_reg.NR50;
				case 0x25: return gb->gb_reg.NR51;
				case 0x26: return gb->gb_reg.NR52;
#endif

						   /* LCD Registers */
				case 0x40: return gb->gb_reg.LCDC;
				case 0x41:
						   return (gb->gb_reg.STAT & STAT_USER_BITS) |
							   (gb->gb_reg.LCDC & LCDC_ENABLE ? gb->lcd_mode : LCD_VBLANK);
				case 0x42: return gb->gb_reg.SCY;
				case 0x43: return gb->gb_reg.SCX;
				case 0x44: return gb->gb_reg.LY;
				case 0x45: return gb->gb_reg.LYC;

						   /* DMA Register */
				case 0x46: return gb->gb_reg.DMA;

						   /* DMG Palette Registers */
				case 0x47: return gb->gb_reg.BGP;
				case 0x48: return gb->gb_reg.OBP0;
				case 0x49: return gb->gb_reg.OBP1;

						   /* Window Position Registers */
				case 0x4A: return gb->gb_reg.WY;
				case 0x4B: return gb->gb_reg.WX;

						   /* Interrupt Enable Register */
				case 0xFF: return gb->gb_reg.IE;

						   /* Unused registers return 1 */
				default: return 1;
			}
	}

	(gb->gb_error)(gb, GB_INVALID_READ, addr);
	return 1;
}

void __gb_write(struct gb_t *gb, const uint16_t addr, const uint8_t val)
{
	switch(addr >> 12)
	{
		case 0x0:
		case 0x1:
			if(gb->mbc == 2 && addr & 0x10)
				return;
			else if(gb->mbc > 0 && gb->cart_ram)
				gb->enable_cart_ram = ((val & 0x0F) == 0x0A);
			return;

		case 0x2:
			if(gb->mbc == 5)
			{
				gb->selected_rom_bank = (gb->selected_rom_bank & 0x100) | val;
				gb->selected_rom_bank =
					gb->selected_rom_bank % gb->num_rom_banks;
				return;
			}
			/* TODO: Check if this should continue to next case. */

		case 0x3:
			if(gb->mbc == 1)
			{
				//selected_rom_bank = val & 0x7;
				gb->selected_rom_bank = (val & 0x1F) | (gb->selected_rom_bank & 0x60);
				if ((gb->selected_rom_bank & 0x1F) == 0x00)
					gb->selected_rom_bank++;
			}
			else if(gb->mbc == 2 && addr & 0x10)
			{
				gb->selected_rom_bank = val & 0x0F;
				if (!gb->selected_rom_bank)
					gb->selected_rom_bank++;
			}
			else if(gb->mbc == 3)
			{
				gb->selected_rom_bank = val & 0x7F;
				if (!gb->selected_rom_bank)
					gb->selected_rom_bank++;
			}
			else if(gb->mbc == 5)
				gb->selected_rom_bank = (val & 0x01) << 8 | (gb->selected_rom_bank & 0xFF);

			gb->selected_rom_bank = gb->selected_rom_bank % gb->num_rom_banks;
			return;

		case 0x4:
		case 0x5:
			if(gb->mbc == 1)
			{
				gb->cart_ram_bank = (val & 3);
				gb->selected_rom_bank = ((val & 3) << 5) | (gb->selected_rom_bank & 0x1F);
				gb->selected_rom_bank = gb->selected_rom_bank % gb->num_rom_banks;
			}
			else if(gb->mbc == 3)
				gb->cart_ram_bank = val;
			else if(gb->mbc == 5)
				gb->cart_ram_bank = (val & 0x0F);
			return;

		case 0x6:
		case 0x7:
			gb->cart_mode_select = (val & 1);
			return;

		case 0x8:
		case 0x9:
			gb->vram[addr - VRAM_ADDR] = val;
			return;

		case 0xA:
		case 0xB:
			if(gb->cart_ram && gb->enable_cart_ram)
			{
				if(gb->mbc == 3 && gb->cart_ram_bank >= 0x08)
					gb->cart_rtc[gb->cart_ram_bank - 0x08] = val;
				else if(gb->cart_mode_select &&
						gb->cart_ram_bank < gb->num_ram_banks)
				{
					gb->gb_cart_ram_write(gb,
							addr - CART_RAM_ADDR + (gb->cart_ram_bank * CRAM_BANK_SIZE), val);
				}
				else if(gb->num_ram_banks)
					gb->gb_cart_ram_write(gb, addr - CART_RAM_ADDR, val);
			}
			return;

		case 0xC:
			gb->wram[addr - WRAM_0_ADDR] = val;
			return;

		case 0xD:
			gb->wram[addr - WRAM_1_ADDR + WRAM_BANK_SIZE] = val;
			return;

		case 0xE:
			gb->wram[addr - ECHO_ADDR] = val;
			return;

		case 0xF:
			if(addr < OAM_ADDR)
			{
				gb->wram[addr - ECHO_ADDR] = val;
				return;
			}
			if(addr < UNUSED_ADDR)
			{
				gb->oam[addr - OAM_ADDR] = val;
				return;
			}
			/* Unusable memory area. */
			if(addr < IO_ADDR)
				return;
			if(HRAM_ADDR <= addr && addr < INTR_EN_ADDR)
			{
				gb->hram[addr - HRAM_ADDR] = val;
				return;
			}
			/* Wave pattern RAM */
			if((addr & 0xFFF0) == 0xFF30)
			{
#if ENABLE_SOUND
				gb->gb_reg.WAV[addr & 0xF] = val;
#endif
				return;
			}
			/* IO, HRAM, and Interrupts. */
			switch(addr & 0xFF)
			{
				/* IO Registers */
				case 0x00:
					gb->gb_reg.P1 = val & 0x30;
					gb_process_joypad(gb);
					return;
				case 0x01: gb->gb_reg.SB = val;		return;
				case 0x02: gb->gb_reg.SC = val;		return;

						   /* Timer Registers */
				case 0x04: gb->gb_reg.DIV = 0x00;	return;
				case 0x05: gb->gb_reg.TIMA = val;	return;
				case 0x06: gb->gb_reg.TMA = val;	return;
				case 0x07:
						   gb->gb_reg.TAC = val;
						   gb->timer.tac_enable = gb->gb_reg.TAC & TAC_ENABLE;
						   gb->timer.tac_rate = gb->gb_reg.TAC & TAC_RATE;
						   return;

						   /* Interrupt Flag Register */
				case 0x0F:
						   gb->gb_reg.IF = val;
						   return;

#if ENABLE_SOUND
						   /* Sound registers */
				case 0x10: gb->gb_reg.NR10 = val;	return;
				case 0x11: gb->gb_reg.NR11 = val;	return;
				case 0x12: gb->gb_reg.NR12 = val;	return;
				case 0x13: gb->gb_reg.NR13 = val;	return;
				case 0x14: gb->gb_reg.NR14 = val;	return;
				case 0x16: gb->gb_reg.NR21 = val;	return;
				case 0x17: gb->gb_reg.NR22 = val;	return;
				case 0x18: gb->gb_reg.NR23 = val;	return;
				case 0x19: gb->gb_reg.NR24 = val;	return;
				case 0x1A: gb->gb_reg.NR30 = val;	return;
				case 0x1B: gb->gb_reg.NR31 = val;	return;
				case 0x1C: gb->gb_reg.NR32 = val;	return;
				case 0x1D: gb->gb_reg.NR33 = val;	return;
				case 0x1E: gb->gb_reg.NR34 = val;	return;
				case 0x20: gb->gb_reg.NR41 = val;	return;
				case 0x21: gb->gb_reg.NR42 = val;	return;
				case 0x22: gb->gb_reg.NR43 = val;	return;
				case 0x23: gb->gb_reg.NR44 = val;	return;
				case 0x24: gb->gb_reg.NR50 = val;	return;
				case 0x25: gb->gb_reg.NR51 = val;	return;
				case 0x26: gb->gb_reg.NR52 = val;	return;
#endif

						   /* LCD Registers */
				case 0x40: gb->gb_reg.LCDC = val;	return;
				case 0x41: gb->gb_reg.STAT = val;	return;
				case 0x42: gb->gb_reg.SCY = val;	return;
				case 0x43: gb->gb_reg.SCX = val;	return;
				case 0x44: gb->gb_reg.LY = val;		return;
				case 0x45: gb->gb_reg.LYC = val;	return;

						   /* DMA Register */
				case 0x46:
						   gb->gb_reg.DMA = (val % 0xF1);
						   for (uint8_t i = 0; i < OAM_SIZE; i++)
							   gb->oam[i] = __gb_read(gb, (gb->gb_reg.DMA << 8) + i);

						   return;

						   /* DMG Palette Registers */
				case 0x47:
						   gb->gb_reg.BGP = val;
						   gb->BGP[0] = (gb->gb_reg.BGP & 0x03);
						   gb->BGP[1] = (gb->gb_reg.BGP >> 2) & 0x03;
						   gb->BGP[2] = (gb->gb_reg.BGP >> 4) & 0x03;
						   gb->BGP[3] = (gb->gb_reg.BGP >> 6) & 0x03;
						   return;

				case 0x48:
						   gb->gb_reg.OBP0 = val;
						   gb->OBJP[0] = (gb->gb_reg.OBP0 & 0x03);
						   gb->OBJP[1] = (gb->gb_reg.OBP0 >> 2) & 0x03;
						   gb->OBJP[2] = (gb->gb_reg.OBP0 >> 4) & 0x03;
						   gb->OBJP[3] = (gb->gb_reg.OBP0 >> 6) & 0x03;
						   return;

				case 0x49:
						   gb->gb_reg.OBP1 = val;
						   gb->OBJP[4] = (gb->gb_reg.OBP1 & 0x03);
						   gb->OBJP[5] = (gb->gb_reg.OBP1 >> 2) & 0x03;
						   gb->OBJP[6] = (gb->gb_reg.OBP1 >> 4) & 0x03;
						   gb->OBJP[7] = (gb->gb_reg.OBP1 >> 6) & 0x03;
						   return;

						   /* Window Position Registers */
				case 0x4A: gb->gb_reg.WY = val;		return;
				case 0x4B: gb->gb_reg.WX = val;		return;

						   /* Turn off boot ROM */
				case 0x50: gb->gb_bios_enable = 0;	return;

						   /* Interrupt Enable Register */
				case 0xFF: gb->gb_reg.IE = val;		return;
			}
	}

	(gb->gb_error)(gb, GB_INVALID_WRITE, addr);
}

/**
 * Initialises startup values.
 */
void __gb_reset(struct gb_t *gb)
{
	gb->gb_halt = 0;
	gb->gb_ime = 1;
	gb->gb_bios_enable = 0;
	gb->lcd_mode = LCD_HBLANK;

	/* Initialise MBC values. */
	gb->selected_rom_bank = 1;
	gb->cart_ram_bank = 0;
	gb->enable_cart_ram = 0;
	gb->cart_mode_select = 0;

	/* Initialise CPU registers as though a DMG. */
	gb->cpu_reg.af = 0x01B0;
	gb->cpu_reg.bc = 0x0013;
	gb->cpu_reg.de = 0x00D8;
	gb->cpu_reg.hl = 0x014D;
	gb->cpu_reg.sp = 0xFFFE;
	/* TODO: Add BIOS support. */
	gb->cpu_reg.pc = 0x0100;

	gb->timer.cpu_count = 0;
	gb->timer.lcd_count = 0;
	gb->timer.div_count = 0;
	gb->timer.tima_count = 0;
	gb->timer.tac_enable = 0;
	gb->timer.tac_rate = 0;

	gb->gb_reg.TIMA      = 0x00;
	gb->gb_reg.TMA       = 0x00;
	gb->gb_reg.TAC       = 0x00;

#if ENABLE_SOUND
	gb->gb_reg.NR10      = 0x80;
	gb->gb_reg.NR11      = 0xBF;
	gb->gb_reg.NR12      = 0xF3;
	gb->gb_reg.NR14      = 0xBF;
	gb->gb_reg.NR21      = 0x3F;
	gb->gb_reg.NR22      = 0x00;
	gb->gb_reg.NR24      = 0xBF;
	gb->gb_reg.NR30      = 0x7F;
	gb->gb_reg.NR31      = 0xFF;
	gb->gb_reg.NR32      = 0x9F;
	gb->gb_reg.NR33      = 0xBF;
	gb->gb_reg.NR41      = 0xFF;
	gb->gb_reg.NR42      = 0x00;
	gb->gb_reg.NR43      = 0x00;
	gb->gb_reg.NR44      = 0xBF;
	gb->gb_reg.NR50      = 0x77;
	gb->gb_reg.NR51      = 0xF3;
	gb->gb_reg.NR52      = 0xF1;
#endif

	gb->gb_reg.LCDC      = 0x91;
	gb->gb_reg.SCY       = 0x00;
	gb->gb_reg.SCX       = 0x00;
	gb->gb_reg.LYC       = 0x00;

	/* Appease valgrind for invalid reads and unconditional jumps. */
	gb->gb_reg.SC = 0;
	gb->gb_reg.STAT = 0;
	gb->cpu_reg.f = 0;
	gb->gb_reg.LY = 0;

	__gb_write(gb, 0xFF47, 0xFC);    // BGP
	__gb_write(gb, 0xFF48, 0xFF);    // OBJP0
	__gb_write(gb, 0xFF49, 0x0F);    // OBJP1
	gb->gb_reg.WY        = 0x00;
	gb->gb_reg.WX        = 0x00;
	gb->gb_reg.IE        = 0x00;

	gb->joypad = 0xFF;
}

void __gb_execute_cb(struct gb_t *gb)
{
	uint8_t cbop = __gb_read(gb, gb->cpu_reg.pc++);
	uint8_t r = (cbop & 0x7);
	uint8_t b = (cbop >> 3) & 0x7;
	uint8_t d = (cbop >> 3) & 0x1;
	uint8_t val;
	uint8_t writeback = 1;

	switch(r)
	{
		case 0: val = gb->cpu_reg.b; break;
		case 1: val = gb->cpu_reg.c; break;
		case 2: val = gb->cpu_reg.d; break;
		case 3: val = gb->cpu_reg.e; break;
		case 4: val = gb->cpu_reg.h; break;
		case 5: val = gb->cpu_reg.l; break;
		case 6: val = __gb_read(gb, gb->cpu_reg.hl); break;

				/* Only values 0-7 are possible here, so we make the final case
				 * default to satisfy -Wmaybe-uninitialized warning. */
		default: val = gb->cpu_reg.a; break;
	}

	/* TODO: Find out WTF this is doing. */
	switch(cbop >> 6)
	{
		case 0x0:
			cbop = (cbop >> 4) & 0x3;
			switch(cbop)
			{
				case 0x0: /* RdC R */
				case 0x1: /* Rd R */
					if(d) /* RRC R / RR R */
					{
						uint8_t temp = val;
						val = (val >> 1);
						val |= cbop ? (gb->cpu_reg.f_bits.c << 7) : (temp << 7);
						gb->cpu_reg.f_bits.z = (val == 0x00);
						gb->cpu_reg.f_bits.n = 0;
						gb->cpu_reg.f_bits.h = 0;
						gb->cpu_reg.f_bits.c = (temp & 0x01);
					}
					else /* RLC R / RL R */
					{
						uint8_t temp = val;
						val = (val << 1);
						val |= cbop ? gb->cpu_reg.f_bits.c : (temp >> 7);
						gb->cpu_reg.f_bits.z = (val == 0x00);
						gb->cpu_reg.f_bits.n = 0;
						gb->cpu_reg.f_bits.h = 0;
						gb->cpu_reg.f_bits.c = (temp >> 7);
					}
					break;
				case 0x2:
					if(d) /* SRA R */
					{
						gb->cpu_reg.f_bits.c = val & 0x01;
						val = (val >> 1) | (val & 0x80);
						gb->cpu_reg.f_bits.z = (val == 0x00);
						gb->cpu_reg.f_bits.n = 0;
						gb->cpu_reg.f_bits.h = 0;
					}
					else /* SLA R */
					{
						gb->cpu_reg.f_bits.c = (val >> 7);
						val = val << 1;
						gb->cpu_reg.f_bits.z = (val == 0x00);
						gb->cpu_reg.f_bits.n = 0;
						gb->cpu_reg.f_bits.h = 0;
					}
					break;
				case 0x3:
					if(d) /* SRL R */
					{
						gb->cpu_reg.f_bits.c = val & 0x01;
						val = val >> 1;
						gb->cpu_reg.f_bits.z = (val == 0x00);
						gb->cpu_reg.f_bits.n = 0;
						gb->cpu_reg.f_bits.h = 0;
					}
					else /* SWAP R */
					{
						uint8_t temp = (val >> 4) & 0x0F;
						temp |= (val << 4) & 0xF0;
						val = temp;
						gb->cpu_reg.f_bits.z = (val == 0x00);
						gb->cpu_reg.f_bits.n = 0;
						gb->cpu_reg.f_bits.h = 0;
						gb->cpu_reg.f_bits.c = 0;
					}
					break;
			}
			break;
		case 0x1: /* BIT B, R */
			gb->cpu_reg.f_bits.z = !((val >> b) & 0x1);
			gb->cpu_reg.f_bits.n = 0;
			gb->cpu_reg.f_bits.h = 1;
			writeback = 0;
			break;
		case 0x2: /* RES B, R */
			val &= (0xFE << b) | (0xFF >> (8 - b));
			break;
		case 0x3: /* SET B, R */
			val |= (0x1 << b);
			break;
	}

	if(writeback)
	{
		switch(r)
		{
			case 0: gb->cpu_reg.b = val; break;
			case 1: gb->cpu_reg.c = val; break;
			case 2: gb->cpu_reg.d = val; break;
			case 3: gb->cpu_reg.e = val; break;
			case 4: gb->cpu_reg.h = val; break;
			case 5: gb->cpu_reg.l = val; break;
			case 6: __gb_write(gb, gb->cpu_reg.hl, val); break;
			case 7: gb->cpu_reg.a = val; break;
		}
	}
}

/* TODO: Completely rewrite this */
#if ENABLE_LCD
void __gb_draw_line(struct gb_t *gb)
{
	uint8_t BX, BY;
	uint8_t WX;
	uint8_t SX[LCD_WIDTH];
	uint16_t bg_line = 0, win_line, tile;
	uint8_t t1, t2, c;
	uint8_t count = 0;

	if(gb->gb_reg.LCDC & LCDC_BG_ENABLE)
	{
		BY = gb->gb_reg.LY + gb->gb_reg.SCY;
		bg_line = (gb->gb_reg.LCDC & LCDC_BG_MAP) ? VRAM_BMAP_2 : VRAM_BMAP_1;
		bg_line += (BY >> 3) * 0x20;
	}
	else
		bg_line = 0;

	if(gb->gb_reg.LCDC & LCDC_WINDOW_ENABLE
			&& gb->gb_reg.LY >= gb->WY && gb->gb_reg.WX <= 166)
	{
		win_line = (gb->gb_reg.LCDC & LCDC_WINDOW_MAP) ? VRAM_BMAP_2 : VRAM_BMAP_1;
		/* TODO: Check this. */
		win_line += (gb->WYC >> 3) * 0x20;
	}
	else
		win_line = 0;

	memset(SX, 0xFF, LCD_WIDTH);

	/* draw background */
	if(bg_line)
	{
		uint8_t X = LCD_WIDTH - 1;
		BX = X + gb->gb_reg.SCX;

		/* TODO: Move declarations to the top. */
		/* lookup tile index */
		uint8_t py = (BY & 0x07);
		uint8_t px = 7 - (BX & 0x07);
		uint8_t idx = gb->vram[bg_line + (BX >> 3)];

		if (gb->gb_reg.LCDC & LCDC_TILE_SELECT)
			tile = VRAM_TILES_1 + idx * 0x10;
		else
			tile = VRAM_TILES_2 + ((idx + 0x80) % 0x100) * 0x10;

		tile += 2*py;

		/* fetch first tile */
		t1 = gb->vram[tile] >> px;
		t2 = gb->vram[tile+1] >> px;

		for (; X != 0xFF; X--)
		{
			SX[X] = 0xFE;
			if (px == 8)
			{
				/* fetch next tile */
				px = 0;
				BX = X + gb->gb_reg.SCX;
				idx = gb->vram[bg_line + (BX >> 3)];
				if (gb->gb_reg.LCDC & LCDC_TILE_SELECT)
					tile = VRAM_TILES_1 + idx * 0x10;
				else
					tile = VRAM_TILES_2 + ((idx + 0x80) % 0x100) * 0x10;
				tile += 2*py;
				t1 = gb->vram[tile];
				t2 = gb->vram[tile+1];
			}
			/* copy background */
			c = (t1 & 0x1) | ((t2 & 0x1) << 1);
			gb->gb_fb[gb->gb_reg.LY][X] = c;// BGP[c];
			t1 = t1 >> 1;
			t2 = t2 >> 1;
			px++;
		}
	}

	/* draw window */
	if(win_line)
	{
		uint8_t X = LCD_WIDTH - 1;
		WX = X - gb->gb_reg.WX + 7;

		// look up tile
		uint8_t py = gb->WYC & 0x07;
		uint8_t px = 7 - (WX & 0x07);
		uint8_t idx = gb->vram[win_line + (WX >> 3)];

		if (gb->gb_reg.LCDC & LCDC_TILE_SELECT)
			tile = VRAM_TILES_1 + idx * 0x10;
		else
			tile = VRAM_TILES_2 + ((idx + 0x80) % 0x100) * 0x10;

		tile += 2*py;

		// fetch first tile
		t1 = gb->vram[tile] >> px;
		t2 = gb->vram[tile+1] >> px;

		// loop & copy window
		uint8_t end = (gb->gb_reg.WX < 7 ? 0 : gb->gb_reg.WX - 7) - 1;

		for (; X != end; X--)
		{
			SX[X] = 0xFE;
			if (px == 8)
			{
				// fetch next tile
				px = 0;
				WX = X - gb->gb_reg.WX + 7;
				idx = gb->vram[win_line + (WX >> 3)];
				if (gb->gb_reg.LCDC & LCDC_TILE_SELECT)
					tile = VRAM_TILES_1 + idx * 0x10;
				else
					tile = VRAM_TILES_2 + ((idx + 0x80) % 0x100) * 0x10;

				tile += 2*py;
				t1 = gb->vram[tile];
				t2 = gb->vram[tile+1];
			}
			// copy window
			c = (t1 & 0x1) | ((t2 & 0x1) << 1);
			gb->gb_fb[gb->gb_reg.LY][X] = c; //BGP[c];
			t1 = t1 >> 1;
			t2 = t2 >> 1;
			px++;
		}
		gb->WYC++; // advance window line
	}

	// draw sprites
	if (gb->gb_reg.LCDC & LCDC_OBJ_ENABLE)
	{
		for(uint8_t s = NUM_SPRITES - 1; s != 0xFF; s--)
			//for (u8 s = 0; s < NUM_SPRITES && count < MAX_SPRITES_LINE; s++)
		{
			uint8_t OY = gb->oam[4*s + 0];
			uint8_t OX = gb->oam[4*s + 1];
			uint8_t OT = gb->oam[4*s + 2] & (gb->gb_reg.LCDC & LCDC_OBJ_SIZE ? 0xFE : 0xFF);
			uint8_t OF = gb->oam[4*s + 3];

			// sprite is on this line
			if (gb->gb_reg.LY + (gb->gb_reg.LCDC & LCDC_OBJ_SIZE ? 0 : 8) < OY && gb->gb_reg.LY + 16 >= OY)
			{
				count++;
				if (OX == 0 || OX >= 168)
					continue;   // but not visible

				// y flip
				uint8_t py = gb->gb_reg.LY - OY + 16;
				if (OF & OBJ_FLIP_Y)
					py = (gb->gb_reg.LCDC & LCDC_OBJ_SIZE ? 15 : 7) - py;

				// fetch the tile
				t1 = gb->vram[VRAM_TILES_1 + OT * 0x10 + 2*py];
				t2 = gb->vram[VRAM_TILES_1 + OT * 0x10 + 2*py + 1];

				// handle x flip
				uint8_t dir, start, end, shift;
				if (OF & OBJ_FLIP_X)
				{
					dir = 1;
					start = (OX < 8 ? 0 : OX - 8);
					end = MIN(OX, LCD_WIDTH);
					shift = 8 - OX + start;
				}
				else
				{
					dir = -1;
					start = MIN(OX, LCD_WIDTH) - 1;
					end = (OX < 8 ? 0 : OX - 8) - 1;
					shift = OX - (start + 1);
				}

				// copy tile
				t1 >>= shift;
				t2 >>= shift;
				for(uint8_t X = start; X != end; X += dir)
				{
					c = (t1 & 0x1) | ((t2 & 0x1) << 1);
					// check transparency / sprite overlap / background overlap
					if (c && OX <= SX[X] &&
							!((OF & OBJ_PRIORITY) && ((gb->gb_fb[gb->gb_reg.LY][X] & 0x3) && SX[X] == 0xFE)))
						//                    if (c && OX <= SX[X] && !(OF & OBJ_PRIORITY && gb_fb[gb->gb_reg.LY][X] & 0x3))
					{
						SX[X] = OX;
						gb->gb_fb[gb->gb_reg.LY][X] = (OF & OBJ_PALETTE) ? gb->OBJP[c + 4] : gb->OBJP[c];
					}
					t1 = t1 >> 1;
					t2 = t2 >> 1;
				}
			}
		}
	}

	/* Convert to 8-bit color. */
	for(uint8_t X = 0; X < LCD_WIDTH; X++)
	{
		if (SX[X] == 0xFE)
			gb->gb_fb[gb->gb_reg.LY][X] = gb->BGP[gb->gb_fb[gb->gb_reg.LY][X]];
	}
}
#endif

void __gb_step_cpu(struct gb_t *gb)
{
	uint8_t opcode, inst_cycles;
	static const uint8_t op_cycles[0x100] = {
		/*  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F		*/
		4,12, 8, 8, 4, 4, 8, 4,20, 8, 8, 8, 4, 4, 8, 4,		/* 0x00 */
		4,12, 8, 8, 4, 4, 8, 4, 8, 8, 8, 8, 4, 4, 8, 4,		/* 0x10 */
		8,12, 8, 8, 4, 4, 8, 4, 8, 8, 8, 8, 4, 4, 8, 4,		/* 0x20 */
		8,12, 8, 8,12,12,12, 4, 8, 8, 8, 8, 4, 4, 8, 4,		/* 0x30 */
		4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,		/* 0x40 */
		4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,		/* 0x50 */
		4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,		/* 0x60 */
		8, 8, 8, 8, 8, 8, 4, 8, 4, 4, 4, 4, 4, 4, 8, 4,		/* 0x70 */
		4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,		/* 0x80 */
		4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,		/* 0x90 */
		4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,		/* 0xA0 */
		4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,		/* 0xB0 */
		8,12,12,12,12,16, 8,32, 8, 8,12, 8,12,12, 8,32,		/* 0xC0 */
		8,12,12, 0,12,16, 8,32, 8, 8,12, 0,12, 0, 8,32,		/* 0xD0 */
		12,12,8, 0, 0,16, 8,32,16, 4,16, 0, 0, 0, 8,32,		/* 0xE0 */
		12,12,8, 4, 0,16, 8,32,12, 8,16, 4, 0, 0, 8,32		/* 0xF0 */
	};

	/* Handle interrupts */
	if((gb->gb_ime || gb->gb_halt) &&
			(gb->gb_reg.IF & gb->gb_reg.IE & ANY_INTR))
	{
		gb->gb_halt = 0;

		if(gb->gb_ime)
		{
			/* Disable interrupts */
			gb->gb_ime = 0;

			/* Push Program Counter */
			__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
			__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);

			/* Call interrupt handler if required. */
			if(gb->gb_reg.IF & gb->gb_reg.IE & VBLANK_INTR)
			{
				gb->cpu_reg.pc = VBLANK_INTR_ADDR;
				gb->gb_reg.IF ^= VBLANK_INTR;
			}
			else if(gb->gb_reg.IF & gb->gb_reg.IE & LCDC_INTR)
			{
				gb->cpu_reg.pc = LCDC_INTR_ADDR;
				gb->gb_reg.IF ^= LCDC_INTR;
			}
			else if(gb->gb_reg.IF & gb->gb_reg.IE & TIMER_INTR)
			{
				gb->cpu_reg.pc = TIMER_INTR_ADDR;
				gb->gb_reg.IF ^= TIMER_INTR;
			}
			else if(gb->gb_reg.IF & gb->gb_reg.IE & SERIAL_INTR)
			{
				gb->cpu_reg.pc = SERIAL_INTR_ADDR;
				gb->gb_reg.IF ^= SERIAL_INTR;
			}
			else if(gb->gb_reg.IF & gb->gb_reg.IE & CONTROL_INTR)
			{
				gb->cpu_reg.pc = CONTROL_INTR_ADDR;
				gb->gb_reg.IF ^= CONTROL_INTR;
			}
		}
	}

	/* Obtain opcode */
	opcode = (gb->gb_halt ? 0x00 : __gb_read(gb, gb->cpu_reg.pc++));
	inst_cycles = op_cycles[opcode];

	/* Execute opcode */
	switch(opcode)
	{
		case 0x00: /* NOP */
			break;
		case 0x01: /* LD BC, imm */
			gb->cpu_reg.c = __gb_read(gb, gb->cpu_reg.pc++);
			gb->cpu_reg.b = __gb_read(gb, gb->cpu_reg.pc++);
			break;
		case 0x02: /* LD (BC), A */
			__gb_write(gb, gb->cpu_reg.bc, gb->cpu_reg.a);
			break;
		case 0x03: /* INC BC */
			gb->cpu_reg.bc++;
			break;
		case 0x04: /* INC B */
			gb->cpu_reg.b++;
			gb->cpu_reg.f_bits.z = (gb->cpu_reg.b == 0x00);
			gb->cpu_reg.f_bits.n = 0;
			gb->cpu_reg.f_bits.h = ((gb->cpu_reg.b & 0x0F) == 0x00);
			break;
		case 0x05: /* DEC B */
			gb->cpu_reg.b--;
			gb->cpu_reg.f_bits.z = (gb->cpu_reg.b == 0x00);
			gb->cpu_reg.f_bits.n = 1;
			gb->cpu_reg.f_bits.h = ((gb->cpu_reg.b & 0x0F) == 0x0F);
			break;
		case 0x06: /* LD B, imm */
			gb->cpu_reg.b = __gb_read(gb, gb->cpu_reg.pc++);
			break;
		case 0x07: /* RLCA */
			gb->cpu_reg.a = (gb->cpu_reg.a << 1) | (gb->cpu_reg.a >> 7);
			gb->cpu_reg.f_bits.z = 0;
			gb->cpu_reg.f_bits.n = 0;
			gb->cpu_reg.f_bits.h = 0;
			gb->cpu_reg.f_bits.c = (gb->cpu_reg.a & 0x01);
			break;
		case 0x08: /* LD (imm), SP */
			{
				uint16_t temp = __gb_read(gb, gb->cpu_reg.pc++);
				temp |= __gb_read(gb, gb->cpu_reg.pc++) << 8;
				__gb_write(gb, temp++, gb->cpu_reg.sp & 0xFF);
				__gb_write(gb, temp, gb->cpu_reg.sp >> 8);
				break;
			}
		case 0x09: /* ADD HL, BC */
			{
				uint32_t temp = gb->cpu_reg.hl + gb->cpu_reg.bc;
				gb->cpu_reg.f_bits.n = 0;
				gb->cpu_reg.f_bits.h =
					(temp ^ gb->cpu_reg.hl ^ gb->cpu_reg.bc) & 0x1000 ? 1 : 0;
				gb->cpu_reg.f_bits.c = (temp & 0xFFFF0000) ? 1 : 0;
				gb->cpu_reg.hl = (temp & 0x0000FFFF);
				break;
			}
		case 0x0A: /* LD A, (BC) */
			gb->cpu_reg.a = __gb_read(gb, gb->cpu_reg.bc);
			break;
		case 0x0B: /* DEC BC */
			gb->cpu_reg.bc--;
			break;
		case 0x0C: /* INC C */
			gb->cpu_reg.c++;
			gb->cpu_reg.f_bits.z = (gb->cpu_reg.c == 0x00);
			gb->cpu_reg.f_bits.n = 0;
			gb->cpu_reg.f_bits.h = ((gb->cpu_reg.c & 0x0F) == 0x00);
			break;
		case 0x0D: /* DEC C */
			gb->cpu_reg.c--;
			gb->cpu_reg.f_bits.z = (gb->cpu_reg.c == 0x00);
			gb->cpu_reg.f_bits.n = 1;
			gb->cpu_reg.f_bits.h = ((gb->cpu_reg.c & 0x0F) == 0x0F);
			break;
		case 0x0E: /* LD C, imm */
			gb->cpu_reg.c = __gb_read(gb, gb->cpu_reg.pc++);
			break;
		case 0x0F: /* RRCA */
			gb->cpu_reg.f_bits.c = gb->cpu_reg.a & 0x01;
			gb->cpu_reg.a = (gb->cpu_reg.a >> 1) | (gb->cpu_reg.a << 7);
			gb->cpu_reg.f_bits.z = 0;
			gb->cpu_reg.f_bits.n = 0;
			gb->cpu_reg.f_bits.h = 0;
			break;
		case 0x10: /* STOP */
			//gb->gb_halt = 1;
			break;
		case 0x11: /* LD DE, imm */
			gb->cpu_reg.e = __gb_read(gb, gb->cpu_reg.pc++);
			gb->cpu_reg.d = __gb_read(gb, gb->cpu_reg.pc++);
			break;
		case 0x12: /* LD (DE), A */
			__gb_write(gb, gb->cpu_reg.de, gb->cpu_reg.a);
			break;
		case 0x13: /* INC DE */
			gb->cpu_reg.de++;
			break;
		case 0x14: /* INC D */
			gb->cpu_reg.d++;
			gb->cpu_reg.f_bits.z = (gb->cpu_reg.d == 0x00);
			gb->cpu_reg.f_bits.n = 0;
			gb->cpu_reg.f_bits.h = ((gb->cpu_reg.d & 0x0F) == 0x00);
			break;
		case 0x15: /* DEC D */
			gb->cpu_reg.d--;
			gb->cpu_reg.f_bits.z = (gb->cpu_reg.d == 0x00);
			gb->cpu_reg.f_bits.n = 1;
			gb->cpu_reg.f_bits.h = ((gb->cpu_reg.d & 0x0F) == 0x0F);
			break;
		case 0x16: /* LD D, imm */
			gb->cpu_reg.d = __gb_read(gb, gb->cpu_reg.pc++);
			break;
		case 0x17: /* RLA */
			{
				uint8_t temp = gb->cpu_reg.a;
				gb->cpu_reg.a = (gb->cpu_reg.a << 1) | gb->cpu_reg.f_bits.c;
				gb->cpu_reg.f_bits.z = 0;
				gb->cpu_reg.f_bits.n = 0;
				gb->cpu_reg.f_bits.h = 0;
				gb->cpu_reg.f_bits.c = (temp >> 7) & 0x01;
				break;
			}
		case 0x18: /* JR imm */
			{
				int8_t temp = (int8_t) __gb_read(gb, gb->cpu_reg.pc++);
				gb->cpu_reg.pc += temp;
				break;
			}
		case 0x19: /* ADD HL, DE */
			{
				uint32_t temp = gb->cpu_reg.hl + gb->cpu_reg.de;
				gb->cpu_reg.f_bits.n = 0;
				gb->cpu_reg.f_bits.h =
					(temp ^ gb->cpu_reg.hl ^ gb->cpu_reg.de) & 0x1000 ? 1 : 0;
				gb->cpu_reg.f_bits.c = (temp & 0xFFFF0000) ? 1 : 0;
				gb->cpu_reg.hl = (temp & 0x0000FFFF);
				break;
			}
		case 0x1A: /* LD A, (DE) */
			gb->cpu_reg.a = __gb_read(gb, gb->cpu_reg.de);
			break;
		case 0x1B: /* DEC DE */
			gb->cpu_reg.de--;
			break;
		case 0x1C: /* INC E */
			gb->cpu_reg.e++;
			gb->cpu_reg.f_bits.z = (gb->cpu_reg.e == 0x00);
			gb->cpu_reg.f_bits.n = 0;
			gb->cpu_reg.f_bits.h = ((gb->cpu_reg.e & 0x0F) == 0x00);
			break;
		case 0x1D: /* DEC E */
			gb->cpu_reg.e--;
			gb->cpu_reg.f_bits.z = (gb->cpu_reg.e == 0x00);
			gb->cpu_reg.f_bits.n = 1;
			gb->cpu_reg.f_bits.h = ((gb->cpu_reg.e & 0x0F) == 0x0F);
			break;
		case 0x1E: /* LD E, imm */
			gb->cpu_reg.e = __gb_read(gb, gb->cpu_reg.pc++);
			break;
		case 0x1F: /* RRA */
			{
				uint8_t temp = gb->cpu_reg.a;
				gb->cpu_reg.a = gb->cpu_reg.a >> 1 | (gb->cpu_reg.f_bits.c << 7);
				gb->cpu_reg.f_bits.z = 0;
				gb->cpu_reg.f_bits.n = 0;
				gb->cpu_reg.f_bits.h = 0;
				gb->cpu_reg.f_bits.c = temp & 0x1;
				break;
			}
		case 0x20: /* JP NZ, imm */
			if(!gb->cpu_reg.f_bits.z)
			{
				int8_t temp = (int8_t) __gb_read(gb, gb->cpu_reg.pc++);
				gb->cpu_reg.pc += temp;
			}
			else
				gb->cpu_reg.pc++;
			break;
		case 0x21: /* LD HL, imm */
			gb->cpu_reg.l = __gb_read(gb, gb->cpu_reg.pc++);
			gb->cpu_reg.h = __gb_read(gb, gb->cpu_reg.pc++);
			break;
		case 0x22: /* LDI (HL), A */
			__gb_write(gb, gb->cpu_reg.hl, gb->cpu_reg.a);
			gb->cpu_reg.hl++;
			break;
		case 0x23: /* INC HL */
			gb->cpu_reg.hl++;
			break;
		case 0x24: /* INC H */
			gb->cpu_reg.h++;
			gb->cpu_reg.f_bits.z = (gb->cpu_reg.h == 0x00);
			gb->cpu_reg.f_bits.n = 0;
			gb->cpu_reg.f_bits.h = ((gb->cpu_reg.h & 0x0F) == 0x00);
			break;
		case 0x25: /* DEC H */
			gb->cpu_reg.h--;
			gb->cpu_reg.f_bits.z = (gb->cpu_reg.h == 0x00);
			gb->cpu_reg.f_bits.n = 1;
			gb->cpu_reg.f_bits.h = ((gb->cpu_reg.h & 0x0F) == 0x0F);
			break;
		case 0x26: /* LD H, imm */
			gb->cpu_reg.h = __gb_read(gb, gb->cpu_reg.pc++);
			break;
		case 0x27: /* DAA */
			{
				uint16_t a = gb->cpu_reg.a;

				if(gb->cpu_reg.f_bits.n)
				{
					if(gb->cpu_reg.f_bits.h)
						a = (a - 0x06) & 0xFF;
					if(gb->cpu_reg.f_bits.c)
						a -= 0x60;
				}
				else
				{
					if(gb->cpu_reg.f_bits.h || (a & 0x0F) > 9)
						a += 0x06;
					if(gb->cpu_reg.f_bits.c || a > 0x9F)
						a += 0x60;
				}

				if((a & 0x100) == 0x100)
					gb->cpu_reg.f_bits.c = 1;

				gb->cpu_reg.a = a;
				gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0);
				gb->cpu_reg.f_bits.h = 0;

				break;
			}
		case 0x28: /* JP Z, imm */
			if(gb->cpu_reg.f_bits.z)
			{
				int8_t temp = (int8_t) __gb_read(gb, gb->cpu_reg.pc++);
				gb->cpu_reg.pc += temp;
			}
			else
				gb->cpu_reg.pc++;
			break;
		case 0x29: /* ADD HL, HL */
			{
				uint32_t temp = gb->cpu_reg.hl + gb->cpu_reg.hl;
				gb->cpu_reg.f_bits.n = 0;
				gb->cpu_reg.f_bits.h = (temp & 0x1000) ? 1 : 0;
				gb->cpu_reg.f_bits.c = (temp & 0xFFFF0000) ? 1 : 0;
				gb->cpu_reg.hl = (temp & 0x0000FFFF);
				break;
			}
		case 0x2A: /* LD A, (HL+) */
			gb->cpu_reg.a = __gb_read(gb, gb->cpu_reg.hl++);
			break;
		case 0x2B: /* DEC HL */
			gb->cpu_reg.hl--;
			break;
		case 0x2C: /* INC L */
			gb->cpu_reg.l++;
			gb->cpu_reg.f_bits.z = (gb->cpu_reg.l == 0x00);
			gb->cpu_reg.f_bits.n = 0;
			gb->cpu_reg.f_bits.h = ((gb->cpu_reg.l & 0x0F) == 0x00);
			break;
		case 0x2D: /* DEC L */
			gb->cpu_reg.l--;
			gb->cpu_reg.f_bits.z = (gb->cpu_reg.l == 0x00);
			gb->cpu_reg.f_bits.n = 1;
			gb->cpu_reg.f_bits.h = ((gb->cpu_reg.l & 0x0F) == 0x0F);
			break;
		case 0x2E: /* LD L, imm */
			gb->cpu_reg.l = __gb_read(gb, gb->cpu_reg.pc++);
			break;
		case 0x2F: /* CPL */
			gb->cpu_reg.a = ~gb->cpu_reg.a;
			gb->cpu_reg.f_bits.n = 1;
			gb->cpu_reg.f_bits.h = 1;
			break;
		case 0x30: /* JP NC, imm */
			if(!gb->cpu_reg.f_bits.c)
			{
				int8_t temp = (int8_t) __gb_read(gb, gb->cpu_reg.pc++);
				gb->cpu_reg.pc += temp;
			}
			else
				gb->cpu_reg.pc++;
			break;
		case 0x31: /* LD SP, imm */
			gb->cpu_reg.sp = __gb_read(gb, gb->cpu_reg.pc++);
			gb->cpu_reg.sp |= __gb_read(gb, gb->cpu_reg.pc++) << 8;
			break;
		case 0x32: /* LD (HL), A */
			__gb_write(gb, gb->cpu_reg.hl, gb->cpu_reg.a);
			gb->cpu_reg.hl--;
			break;
		case 0x33: /* INC SP */
			gb->cpu_reg.sp++;
			break;
		case 0x34: /* INC (HL) */
			{
				uint8_t temp = __gb_read(gb, gb->cpu_reg.hl) + 1;
				gb->cpu_reg.f_bits.z = (temp == 0x00);
				gb->cpu_reg.f_bits.n = 0;
				gb->cpu_reg.f_bits.h = ((temp & 0x0F) == 0x00);
				__gb_write(gb, gb->cpu_reg.hl, temp);
				break;
			}
		case 0x35: /* DEC (HL) */
			{
				uint8_t temp = __gb_read(gb, gb->cpu_reg.hl) - 1;
				gb->cpu_reg.f_bits.z = (temp == 0x00);
				gb->cpu_reg.f_bits.n = 1;
				gb->cpu_reg.f_bits.h = ((temp & 0x0F) == 0x0F);
				__gb_write(gb, gb->cpu_reg.hl, temp);
				break;
			}
		case 0x36: /* LD (HL), imm */
			__gb_write(gb, gb->cpu_reg.hl, __gb_read(gb, gb->cpu_reg.pc++));
			break;
		case 0x37: /* SCF */
			gb->cpu_reg.f_bits.n = 0;
			gb->cpu_reg.f_bits.h = 0;
			gb->cpu_reg.f_bits.c = 1;
			break;
		case 0x38: /* JP C, imm */
			if(gb->cpu_reg.f_bits.c)
			{
				int8_t temp = (int8_t) __gb_read(gb, gb->cpu_reg.pc++);
				gb->cpu_reg.pc += temp;
			}
			else
				gb->cpu_reg.pc++;
			break;
		case 0x39: /* ADD HL, SP */
			{
				uint32_t temp = gb->cpu_reg.hl + gb->cpu_reg.sp;
				gb->cpu_reg.f_bits.n = 0;
				gb->cpu_reg.f_bits.h =
					((gb->cpu_reg.hl & 0xFFF) + (gb->cpu_reg.sp & 0xFFF)) & 0x1000 ? 1 : 0;
				gb->cpu_reg.f_bits.c = temp & 0x10000 ? 1 : 0;
				gb->cpu_reg.hl = (uint16_t)temp;
				break;
			}
		case 0x3A: /* LD A, (HL) */
			gb->cpu_reg.a = __gb_read(gb, gb->cpu_reg.hl--);
			break;
		case 0x3B: /* DEC SP */
			gb->cpu_reg.sp--;
			break;
		case 0x3C: /* INC A */
			gb->cpu_reg.a++;
			gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
			gb->cpu_reg.f_bits.n = 0;
			gb->cpu_reg.f_bits.h = ((gb->cpu_reg.a & 0x0F) == 0x00);
			break;
		case 0x3D: /* DEC A */
			gb->cpu_reg.a--;
			gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
			gb->cpu_reg.f_bits.n = 1;
			gb->cpu_reg.f_bits.h = ((gb->cpu_reg.a & 0x0F) == 0x0F);
			break;
		case 0x3E: /* LD A, imm */
			gb->cpu_reg.a = __gb_read(gb, gb->cpu_reg.pc++);
			break;
		case 0x3F: /* CCF */
			gb->cpu_reg.f_bits.n = 0;
			gb->cpu_reg.f_bits.h = 0;
			gb->cpu_reg.f_bits.c = ~gb->cpu_reg.f_bits.c;
			break;
		case 0x40: /* LD B, B */
			break;
		case 0x41: /* LD B, C */
			gb->cpu_reg.b = gb->cpu_reg.c;
			break;
		case 0x42: /* LD B, D */
			gb->cpu_reg.b = gb->cpu_reg.d;
			break;
		case 0x43: /* LD B, E */
			gb->cpu_reg.b = gb->cpu_reg.e;
			break;
		case 0x44: /* LD B, H */
			gb->cpu_reg.b = gb->cpu_reg.h;
			break;
		case 0x45: /* LD B, L */
			gb->cpu_reg.b = gb->cpu_reg.l;
			break;
		case 0x46: /* LD B, (HL) */
			gb->cpu_reg.b = __gb_read(gb, gb->cpu_reg.hl);
			break;
		case 0x47: /* LD B, A */
			gb->cpu_reg.b = gb->cpu_reg.a;
			break;
		case 0x48: /* LD C, B */
			gb->cpu_reg.c = gb->cpu_reg.b;
			break;
		case 0x49: /* LD C, C */
			break;
		case 0x4A: /* LD C, D */
			gb->cpu_reg.c = gb->cpu_reg.d;
			break;
		case 0x4B: /* LD C, E */
			gb->cpu_reg.c = gb->cpu_reg.e;
			break;
		case 0x4C: /* LD C, H */
			gb->cpu_reg.c = gb->cpu_reg.h;
			break;
		case 0x4D: /* LD C, L */
			gb->cpu_reg.c = gb->cpu_reg.l;
			break;
		case 0x4E: /* LD C, (HL) */
			gb->cpu_reg.c = __gb_read(gb, gb->cpu_reg.hl);
			break;
		case 0x4F: /* LD C, A */
			gb->cpu_reg.c = gb->cpu_reg.a;
			break;
		case 0x50: /* LD D, B */
			gb->cpu_reg.d = gb->cpu_reg.b;
			break;
		case 0x51: /* LD D, C */
			gb->cpu_reg.d = gb->cpu_reg.c;
			break;
		case 0x52: /* LD D, D */
			break;
		case 0x53: /* LD D, E */
			gb->cpu_reg.d = gb->cpu_reg.e;
			break;
		case 0x54: /* LD D, H */
			gb->cpu_reg.d = gb->cpu_reg.h;
			break;
		case 0x55: /* LD D, L */
			gb->cpu_reg.d = gb->cpu_reg.l;
			break;
		case 0x56: /* LD D, (HL) */
			gb->cpu_reg.d = __gb_read(gb, gb->cpu_reg.hl);
			break;
		case 0x57: /* LD D, A */
			gb->cpu_reg.d = gb->cpu_reg.a;
			break;
		case 0x58: /* LD E, B */
			gb->cpu_reg.e = gb->cpu_reg.b;
			break;
		case 0x59: /* LD E, C */
			gb->cpu_reg.e = gb->cpu_reg.c;
			break;
		case 0x5A: /* LD E, D */
			gb->cpu_reg.e = gb->cpu_reg.d;
			break;
		case 0x5B: /* LD E, E */
			break;
		case 0x5C: /* LD E, H */
			gb->cpu_reg.e = gb->cpu_reg.h;
			break;
		case 0x5D: /* LD E, L */
			gb->cpu_reg.e = gb->cpu_reg.l;
			break;
		case 0x5E: /* LD E, (HL) */
			gb->cpu_reg.e = __gb_read(gb, gb->cpu_reg.hl);
			break;
		case 0x5F: /* LD E, A */
			gb->cpu_reg.e = gb->cpu_reg.a;
			break;
		case 0x60: /* LD H, B */
			gb->cpu_reg.h = gb->cpu_reg.b;
			break;
		case 0x61: /* LD H, C */
			gb->cpu_reg.h = gb->cpu_reg.c;
			break;
		case 0x62: /* LD H, D */
			gb->cpu_reg.h = gb->cpu_reg.d;
			break;
		case 0x63: /* LD H, E */
			gb->cpu_reg.h = gb->cpu_reg.e;
			break;
		case 0x64: /* LD H, H */
			break;
		case 0x65: /* LD H, L */
			gb->cpu_reg.h = gb->cpu_reg.l;
			break;
		case 0x66: /* LD H, (HL) */
			gb->cpu_reg.h = __gb_read(gb, gb->cpu_reg.hl);
			break;
		case 0x67: /* LD H, A */
			gb->cpu_reg.h = gb->cpu_reg.a;
			break;
		case 0x68: /* LD L, B */
			gb->cpu_reg.l = gb->cpu_reg.b;
			break;
		case 0x69: /* LD L, C */
			gb->cpu_reg.l = gb->cpu_reg.c;
			break;
		case 0x6A: /* LD L, D */
			gb->cpu_reg.l = gb->cpu_reg.d;
			break;
		case 0x6B: /* LD L, E */
			gb->cpu_reg.l = gb->cpu_reg.e;
			break;
		case 0x6C: /* LD L, H */
			gb->cpu_reg.l = gb->cpu_reg.h;
			break;
		case 0x6D: /* LD L, L */
			break;
		case 0x6E: /* LD L, (HL) */
			gb->cpu_reg.l = __gb_read(gb, gb->cpu_reg.hl);
			break;
		case 0x6F: /* LD L, A */
			gb->cpu_reg.l = gb->cpu_reg.a;
			break;
		case 0x70: /* LD (HL), B */
			__gb_write(gb, gb->cpu_reg.hl, gb->cpu_reg.b);
			break;
		case 0x71: /* LD (HL), C */
			__gb_write(gb, gb->cpu_reg.hl, gb->cpu_reg.c);
			break;
		case 0x72: /* LD (HL), D */
			__gb_write(gb, gb->cpu_reg.hl, gb->cpu_reg.d);
			break;
		case 0x73: /* LD (HL), E */
			__gb_write(gb, gb->cpu_reg.hl, gb->cpu_reg.e);
			break;
		case 0x74: /* LD (HL), H */
			__gb_write(gb, gb->cpu_reg.hl, gb->cpu_reg.h);
			break;
		case 0x75: /* LD (HL), L */
			__gb_write(gb, gb->cpu_reg.hl, gb->cpu_reg.l);
			break;
		case 0x76: /* HALT */
			/* TODO: Emulate HALT bug? */
			gb->gb_halt = 1;
			break;
		case 0x77: /* LD (HL), A */
			__gb_write(gb, gb->cpu_reg.hl, gb->cpu_reg.a);
			break;
		case 0x78: /* LD A, B */
			gb->cpu_reg.a = gb->cpu_reg.b;
			break;
		case 0x79: /* LD A, C */
			gb->cpu_reg.a = gb->cpu_reg.c;
			break;
		case 0x7A: /* LD A, D */
			gb->cpu_reg.a = gb->cpu_reg.d;
			break;
		case 0x7B: /* LD A, E */
			gb->cpu_reg.a = gb->cpu_reg.e;
			break;
		case 0x7C: /* LD A, H */
			gb->cpu_reg.a = gb->cpu_reg.h;
			break;
		case 0x7D: /* LD A, L */
			gb->cpu_reg.a = gb->cpu_reg.l;
			break;
		case 0x7E: /* LD A, (HL) */
			gb->cpu_reg.a = __gb_read(gb, gb->cpu_reg.hl);
			break;
		case 0x7F: /* LD A, A */
			break;
		case 0x80: /* ADD A, B */
			{
				uint16_t temp = gb->cpu_reg.a + gb->cpu_reg.b;
				gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
				gb->cpu_reg.f_bits.n = 0;
				gb->cpu_reg.f_bits.h =
					(gb->cpu_reg.a ^ gb->cpu_reg.b ^ temp) & 0x10 ? 1 : 0;
				gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
				gb->cpu_reg.a = (temp & 0xFF);
				break;
			}
		case 0x81: /* ADD A, C */
			{
				uint16_t temp = gb->cpu_reg.a + gb->cpu_reg.c;
				gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
				gb->cpu_reg.f_bits.n = 0;
				gb->cpu_reg.f_bits.h =
					(gb->cpu_reg.a ^ gb->cpu_reg.c ^ temp) & 0x10 ? 1 : 0;
				gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
				gb->cpu_reg.a = (temp & 0xFF);
				break;
			}
		case 0x82: /* ADD A, D */
			{
				uint16_t temp = gb->cpu_reg.a + gb->cpu_reg.d;
				gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
				gb->cpu_reg.f_bits.n = 0;
				gb->cpu_reg.f_bits.h =
					(gb->cpu_reg.a ^ gb->cpu_reg.d ^ temp) & 0x10 ? 1 : 0;
				gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
				gb->cpu_reg.a = (temp & 0xFF);
				break;
			}
		case 0x83: /* ADD A, E */
			{
				uint16_t temp = gb->cpu_reg.a + gb->cpu_reg.e;
				gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
				gb->cpu_reg.f_bits.n = 0;
				gb->cpu_reg.f_bits.h =
					(gb->cpu_reg.a ^ gb->cpu_reg.e ^ temp) & 0x10 ? 1 : 0;
				gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
				gb->cpu_reg.a = (temp & 0xFF);
				break;
			}
		case 0x84: /* ADD A, H */
			{
				uint16_t temp = gb->cpu_reg.a + gb->cpu_reg.h;
				gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
				gb->cpu_reg.f_bits.n = 0;
				gb->cpu_reg.f_bits.h =
					(gb->cpu_reg.a ^ gb->cpu_reg.h ^ temp) & 0x10 ? 1 : 0;
				gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
				gb->cpu_reg.a = (temp & 0xFF);
				break;
			}
		case 0x85: /* ADD A, L */
			{
				uint16_t temp = gb->cpu_reg.a + gb->cpu_reg.l;
				gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
				gb->cpu_reg.f_bits.n = 0;
				gb->cpu_reg.f_bits.h =
					(gb->cpu_reg.a ^ gb->cpu_reg.l ^ temp) & 0x10 ? 1 : 0;
				gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
				gb->cpu_reg.a = (temp & 0xFF);
				break;
			}
		case 0x86: /* ADD A, (HL) */
			{
				uint8_t hl = __gb_read(gb, gb->cpu_reg.hl);
				uint16_t temp = gb->cpu_reg.a + hl;
				gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
				gb->cpu_reg.f_bits.n = 0;
				gb->cpu_reg.f_bits.h =
					(gb->cpu_reg.a ^ hl ^ temp) & 0x10 ? 1 : 0;
				gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
				gb->cpu_reg.a = (temp & 0xFF);
				break;
			}
		case 0x87: /* ADD A, A */
			{
				uint16_t temp = gb->cpu_reg.a + gb->cpu_reg.a;
				gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
				gb->cpu_reg.f_bits.n = 0;
				gb->cpu_reg.f_bits.h = temp & 0x10 ? 1 : 0;
				gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
				gb->cpu_reg.a = (temp & 0xFF);
				break;
			}
		case 0x88: /* ADC A, B */
			{
				uint16_t temp = gb->cpu_reg.a + gb->cpu_reg.b + gb->cpu_reg.f_bits.c;
				gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
				gb->cpu_reg.f_bits.n = 0;
				gb->cpu_reg.f_bits.h =
					(gb->cpu_reg.a ^ gb->cpu_reg.b ^ temp) & 0x10 ? 1 : 0;
				gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
				gb->cpu_reg.a = (temp & 0xFF);
				break;
			}
		case 0x89: /* ADC A, C */
			{
				uint16_t temp = gb->cpu_reg.a + gb->cpu_reg.c + gb->cpu_reg.f_bits.c;
				gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
				gb->cpu_reg.f_bits.n = 0;
				gb->cpu_reg.f_bits.h =
					(gb->cpu_reg.a ^ gb->cpu_reg.c ^ temp) & 0x10 ? 1 : 0;
				gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
				gb->cpu_reg.a = (temp & 0xFF);
				break;
			}
		case 0x8A: /* ADC A, D */
			{
				uint16_t temp = gb->cpu_reg.a + gb->cpu_reg.d + gb->cpu_reg.f_bits.c;
				gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
				gb->cpu_reg.f_bits.n = 0;
				gb->cpu_reg.f_bits.h =
					(gb->cpu_reg.a ^ gb->cpu_reg.d ^ temp) & 0x10 ? 1 : 0;
				gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
				gb->cpu_reg.a = (temp & 0xFF);
				break;
			}
		case 0x8B: /* ADC A, E */
			{
				uint16_t temp = gb->cpu_reg.a + gb->cpu_reg.e + gb->cpu_reg.f_bits.c;
				gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
				gb->cpu_reg.f_bits.n = 0;
				gb->cpu_reg.f_bits.h =
					(gb->cpu_reg.a ^ gb->cpu_reg.e ^ temp) & 0x10 ? 1 : 0;
				gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
				gb->cpu_reg.a = (temp & 0xFF);
				break;
			}
		case 0x8C: /* ADC A, H */
			{
				uint16_t temp = gb->cpu_reg.a + gb->cpu_reg.h + gb->cpu_reg.f_bits.c;
				gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
				gb->cpu_reg.f_bits.n = 0;
				gb->cpu_reg.f_bits.h =
					(gb->cpu_reg.a ^ gb->cpu_reg.h ^ temp) & 0x10 ? 1 : 0;
				gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
				gb->cpu_reg.a = (temp & 0xFF);
				break;
			}
		case 0x8D: /* ADC A, L */
			{
				uint16_t temp = gb->cpu_reg.a + gb->cpu_reg.l + gb->cpu_reg.f_bits.c;
				gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
				gb->cpu_reg.f_bits.n = 0;
				gb->cpu_reg.f_bits.h =
					(gb->cpu_reg.a ^ gb->cpu_reg.l ^ temp) & 0x10 ? 1 : 0;
				gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
				gb->cpu_reg.a = (temp & 0xFF);
				break;
			}
		case 0x8E: /* ADC A, (HL) */
			{
				uint8_t val = __gb_read(gb, gb->cpu_reg.hl);
				uint16_t temp = gb->cpu_reg.a + val + gb->cpu_reg.f_bits.c;
				gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
				gb->cpu_reg.f_bits.n = 0;
				gb->cpu_reg.f_bits.h =
					(gb->cpu_reg.a ^ val ^ temp) & 0x10 ? 1 : 0;
				gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
				gb->cpu_reg.a = (temp & 0xFF);
				break;
			}
		case 0x8F: /* ADC A, A */
			{
				uint16_t temp = gb->cpu_reg.a + gb->cpu_reg.a + gb->cpu_reg.f_bits.c;
				gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
				gb->cpu_reg.f_bits.n = 0;
				/* TODO: Optimisation here? */
				gb->cpu_reg.f_bits.h =
					(gb->cpu_reg.a ^ gb->cpu_reg.a ^ temp) & 0x10 ? 1 : 0;
				gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
				gb->cpu_reg.a = (temp & 0xFF);
				break;
			}
		case 0x90: /* SUB B */
			{
				uint16_t temp = gb->cpu_reg.a - gb->cpu_reg.b;
				gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
				gb->cpu_reg.f_bits.n = 1;
				gb->cpu_reg.f_bits.h =
					(gb->cpu_reg.a ^ gb->cpu_reg.b ^ temp) & 0x10 ? 1 : 0;
				gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
				gb->cpu_reg.a = (temp & 0xFF);
				break;
			}
		case 0x91: /* SUB C */
			{
				uint16_t temp = gb->cpu_reg.a - gb->cpu_reg.c;
				gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
				gb->cpu_reg.f_bits.n = 1;
				gb->cpu_reg.f_bits.h =
					(gb->cpu_reg.a ^ gb->cpu_reg.c ^ temp) & 0x10 ? 1 : 0;
				gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
				gb->cpu_reg.a = (temp & 0xFF);
				break;
			}
		case 0x92: /* SUB D */
			{
				uint16_t temp = gb->cpu_reg.a - gb->cpu_reg.d;
				gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
				gb->cpu_reg.f_bits.n = 1;
				gb->cpu_reg.f_bits.h =
					(gb->cpu_reg.a ^ gb->cpu_reg.d ^ temp) & 0x10 ? 1 : 0;
				gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
				gb->cpu_reg.a = (temp & 0xFF);
				break;
			}
		case 0x93: /* SUB E */
			{
				uint16_t temp = gb->cpu_reg.a - gb->cpu_reg.e;
				gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
				gb->cpu_reg.f_bits.n = 1;
				gb->cpu_reg.f_bits.h =
					(gb->cpu_reg.a ^ gb->cpu_reg.e ^ temp) & 0x10 ? 1 : 0;
				gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
				gb->cpu_reg.a = (temp & 0xFF);
				break;
			}
		case 0x94: /* SUB H */
			{
				uint16_t temp = gb->cpu_reg.a - gb->cpu_reg.h;
				gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
				gb->cpu_reg.f_bits.n = 1;
				gb->cpu_reg.f_bits.h =
					(gb->cpu_reg.a ^ gb->cpu_reg.h ^ temp) & 0x10 ? 1 : 0;
				gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
				gb->cpu_reg.a = (temp & 0xFF);
				break;
			}
		case 0x95: /* SUB L */
			{
				uint16_t temp = gb->cpu_reg.a - gb->cpu_reg.l;
				gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
				gb->cpu_reg.f_bits.n = 1;
				gb->cpu_reg.f_bits.h =
					(gb->cpu_reg.a ^ gb->cpu_reg.l ^ temp) & 0x10 ? 1 : 0;
				gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
				gb->cpu_reg.a = (temp & 0xFF);
				break;
			}
		case 0x96: /* SUB (HL) */
			{
				uint8_t val = __gb_read(gb, gb->cpu_reg.hl);
				uint16_t temp = gb->cpu_reg.a - val;
				gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
				gb->cpu_reg.f_bits.n = 1;
				gb->cpu_reg.f_bits.h =
					(gb->cpu_reg.a ^ val ^ temp) & 0x10 ? 1 : 0;
				gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
				gb->cpu_reg.a = (temp & 0xFF);
				break;
			}
		case 0x97: /* SUB A */
			gb->cpu_reg.a = 0;
			gb->cpu_reg.f_bits.z = 1;
			gb->cpu_reg.f_bits.n = 1;
			gb->cpu_reg.f_bits.h = 0;
			gb->cpu_reg.f_bits.c = 0;
			break;
		case 0x98: /* SBC A, B */
			{
				uint16_t temp = gb->cpu_reg.a - gb->cpu_reg.b - gb->cpu_reg.f_bits.c;
				gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
				gb->cpu_reg.f_bits.n = 1;
				gb->cpu_reg.f_bits.h =
					(gb->cpu_reg.a ^ gb->cpu_reg.b ^ temp) & 0x10 ? 1 : 0;
				gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
				gb->cpu_reg.a = (temp & 0xFF);
				break;
			}
		case 0x99: /* SBC A, C */
			{
				uint16_t temp = gb->cpu_reg.a - gb->cpu_reg.c - gb->cpu_reg.f_bits.c;
				gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
				gb->cpu_reg.f_bits.n = 1;
				gb->cpu_reg.f_bits.h =
					(gb->cpu_reg.a ^ gb->cpu_reg.c ^ temp) & 0x10 ? 1 : 0;
				gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
				gb->cpu_reg.a = (temp & 0xFF);
				break;
			}
		case 0x9A: /* SBC A, D */
			{
				uint16_t temp = gb->cpu_reg.a - gb->cpu_reg.d - gb->cpu_reg.f_bits.c;
				gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
				gb->cpu_reg.f_bits.n = 1;
				gb->cpu_reg.f_bits.h =
					(gb->cpu_reg.a ^ gb->cpu_reg.d ^ temp) & 0x10 ? 1 : 0;
				gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
				gb->cpu_reg.a = (temp & 0xFF);
				break;
			}
		case 0x9B: /* SBC A, E */
			{
				uint16_t temp = gb->cpu_reg.a - gb->cpu_reg.e - gb->cpu_reg.f_bits.c;
				gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
				gb->cpu_reg.f_bits.n = 1;
				gb->cpu_reg.f_bits.h =
					(gb->cpu_reg.a ^ gb->cpu_reg.e ^ temp) & 0x10 ? 1 : 0;
				gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
				gb->cpu_reg.a = (temp & 0xFF);
				break;
			}
		case 0x9C: /* SBC A, H */
			{
				uint16_t temp = gb->cpu_reg.a - gb->cpu_reg.h - gb->cpu_reg.f_bits.c;
				gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
				gb->cpu_reg.f_bits.n = 1;
				gb->cpu_reg.f_bits.h =
					(gb->cpu_reg.a ^ gb->cpu_reg.h ^ temp) & 0x10 ? 1 : 0;
				gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
				gb->cpu_reg.a = (temp & 0xFF);
				break;
			}
		case 0x9D: /* SBC A, L */
			{
				uint16_t temp = gb->cpu_reg.a - gb->cpu_reg.l - gb->cpu_reg.f_bits.c;
				gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
				gb->cpu_reg.f_bits.n = 1;
				gb->cpu_reg.f_bits.h =
					(gb->cpu_reg.a ^ gb->cpu_reg.l ^ temp) & 0x10 ? 1 : 0;
				gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
				gb->cpu_reg.a = (temp & 0xFF);
				break;
			}
		case 0x9E: /* SBC A, (HL) */
			{
				uint8_t val = __gb_read(gb, gb->cpu_reg.hl);
				uint16_t temp = gb->cpu_reg.a - val - gb->cpu_reg.f_bits.c;
				gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
				gb->cpu_reg.f_bits.n = 1;
				gb->cpu_reg.f_bits.h =
					(gb->cpu_reg.a ^ val ^ temp) & 0x10 ? 1 : 0;
				gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
				gb->cpu_reg.a = (temp & 0xFF);
				break;
			}
		case 0x9F: /* SBC A, A */
			gb->cpu_reg.a = gb->cpu_reg.f_bits.c ? 0xFF : 0x00;
			gb->cpu_reg.f_bits.z = gb->cpu_reg.f_bits.c ? 0x00 : 0x01;
			gb->cpu_reg.f_bits.n = 1;
			gb->cpu_reg.f_bits.h = gb->cpu_reg.f_bits.c;
			break;
		case 0xA0: /* AND B */
			gb->cpu_reg.a = gb->cpu_reg.a & gb->cpu_reg.b;
			gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
			gb->cpu_reg.f_bits.n = 0;
			gb->cpu_reg.f_bits.h = 1;
			gb->cpu_reg.f_bits.c = 0;
			break;
		case 0xA1: /* AND C */
			gb->cpu_reg.a = gb->cpu_reg.a & gb->cpu_reg.c;
			gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
			gb->cpu_reg.f_bits.n = 0;
			gb->cpu_reg.f_bits.h = 1;
			gb->cpu_reg.f_bits.c = 0;
			break;
		case 0xA2: /* AND D */
			gb->cpu_reg.a = gb->cpu_reg.a & gb->cpu_reg.d;
			gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
			gb->cpu_reg.f_bits.n = 0;
			gb->cpu_reg.f_bits.h = 1;
			gb->cpu_reg.f_bits.c = 0;
			break;
		case 0xA3: /* AND E */
			gb->cpu_reg.a = gb->cpu_reg.a & gb->cpu_reg.e;
			gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
			gb->cpu_reg.f_bits.n = 0;
			gb->cpu_reg.f_bits.h = 1;
			gb->cpu_reg.f_bits.c = 0;
			break;
		case 0xA4: /* AND H */
			gb->cpu_reg.a = gb->cpu_reg.a & gb->cpu_reg.h;
			gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
			gb->cpu_reg.f_bits.n = 0;
			gb->cpu_reg.f_bits.h = 1;
			gb->cpu_reg.f_bits.c = 0;
			break;
		case 0xA5: /* AND L */
			gb->cpu_reg.a = gb->cpu_reg.a & gb->cpu_reg.l;
			gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
			gb->cpu_reg.f_bits.n = 0;
			gb->cpu_reg.f_bits.h = 1;
			gb->cpu_reg.f_bits.c = 0;
			break;
		case 0xA6: /* AND B */
			gb->cpu_reg.a = gb->cpu_reg.a & __gb_read(gb, gb->cpu_reg.hl);
			gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
			gb->cpu_reg.f_bits.n = 0;
			gb->cpu_reg.f_bits.h = 1;
			gb->cpu_reg.f_bits.c = 0;
			break;
		case 0xA7: /* AND A */
			gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
			gb->cpu_reg.f_bits.n = 0;
			gb->cpu_reg.f_bits.h = 1;
			gb->cpu_reg.f_bits.c = 0;
			break;
		case 0xA8: /* XOR B */
			gb->cpu_reg.a = gb->cpu_reg.a ^ gb->cpu_reg.b;
			gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
			gb->cpu_reg.f_bits.n = 0;
			gb->cpu_reg.f_bits.h = 0;
			gb->cpu_reg.f_bits.c = 0;
			break;
		case 0xA9: /* XOR C */
			gb->cpu_reg.a = gb->cpu_reg.a ^ gb->cpu_reg.c;
			gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
			gb->cpu_reg.f_bits.n = 0;
			gb->cpu_reg.f_bits.h = 0;
			gb->cpu_reg.f_bits.c = 0;
			break;
		case 0xAA: /* XOR D */
			gb->cpu_reg.a = gb->cpu_reg.a ^ gb->cpu_reg.d;
			gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
			gb->cpu_reg.f_bits.n = 0;
			gb->cpu_reg.f_bits.h = 0;
			gb->cpu_reg.f_bits.c = 0;
			break;
		case 0xAB: /* XOR E */
			gb->cpu_reg.a = gb->cpu_reg.a ^ gb->cpu_reg.e;
			gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
			gb->cpu_reg.f_bits.n = 0;
			gb->cpu_reg.f_bits.h = 0;
			gb->cpu_reg.f_bits.c = 0;
			break;
		case 0xAC: /* XOR H */
			gb->cpu_reg.a = gb->cpu_reg.a ^ gb->cpu_reg.h;
			gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
			gb->cpu_reg.f_bits.n = 0;
			gb->cpu_reg.f_bits.h = 0;
			gb->cpu_reg.f_bits.c = 0;
			break;
		case 0xAD: /* XOR L */
			gb->cpu_reg.a = gb->cpu_reg.a ^ gb->cpu_reg.l;
			gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
			gb->cpu_reg.f_bits.n = 0;
			gb->cpu_reg.f_bits.h = 0;
			gb->cpu_reg.f_bits.c = 0;
			break;
		case 0xAE: /* XOR (HL) */
			gb->cpu_reg.a = gb->cpu_reg.a ^ __gb_read(gb, gb->cpu_reg.hl);
			gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
			gb->cpu_reg.f_bits.n = 0;
			gb->cpu_reg.f_bits.h = 0;
			gb->cpu_reg.f_bits.c = 0;
			break;
		case 0xAF: /* XOR A */
			gb->cpu_reg.a = 0x00;
			gb->cpu_reg.f_bits.z = 1;
			gb->cpu_reg.f_bits.n = 0;
			gb->cpu_reg.f_bits.h = 0;
			gb->cpu_reg.f_bits.c = 0;
			break;
		case 0xB0: /* OR B */
			gb->cpu_reg.a = gb->cpu_reg.a | gb->cpu_reg.b;
			gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
			gb->cpu_reg.f_bits.n = 0;
			gb->cpu_reg.f_bits.h = 0;
			gb->cpu_reg.f_bits.c = 0;
			break;
		case 0xB1: /* OR C */
			gb->cpu_reg.a = gb->cpu_reg.a | gb->cpu_reg.c;
			gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
			gb->cpu_reg.f_bits.n = 0;
			gb->cpu_reg.f_bits.h = 0;
			gb->cpu_reg.f_bits.c = 0;
			break;
		case 0xB2: /* OR D */
			gb->cpu_reg.a = gb->cpu_reg.a | gb->cpu_reg.d;
			gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
			gb->cpu_reg.f_bits.n = 0;
			gb->cpu_reg.f_bits.h = 0;
			gb->cpu_reg.f_bits.c = 0;
			break;
		case 0xB3: /* OR E */
			gb->cpu_reg.a = gb->cpu_reg.a | gb->cpu_reg.e;
			gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
			gb->cpu_reg.f_bits.n = 0;
			gb->cpu_reg.f_bits.h = 0;
			gb->cpu_reg.f_bits.c = 0;
			break;
		case 0xB4: /* OR H */
			gb->cpu_reg.a = gb->cpu_reg.a | gb->cpu_reg.h;
			gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
			gb->cpu_reg.f_bits.n = 0;
			gb->cpu_reg.f_bits.h = 0;
			gb->cpu_reg.f_bits.c = 0;
			break;
		case 0xB5: /* OR L */
			gb->cpu_reg.a = gb->cpu_reg.a | gb->cpu_reg.l;
			gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
			gb->cpu_reg.f_bits.n = 0;
			gb->cpu_reg.f_bits.h = 0;
			gb->cpu_reg.f_bits.c = 0;
			break;
		case 0xB6: /* OR (HL) */
			gb->cpu_reg.a = gb->cpu_reg.a | __gb_read(gb, gb->cpu_reg.hl);
			gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
			gb->cpu_reg.f_bits.n = 0;
			gb->cpu_reg.f_bits.h = 0;
			gb->cpu_reg.f_bits.c = 0;
			break;
		case 0xB7: /* OR A */
			gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
			gb->cpu_reg.f_bits.n = 0;
			gb->cpu_reg.f_bits.h = 0;
			gb->cpu_reg.f_bits.c = 0;
			break;
		case 0xB8: /* CP B */
			{
				uint16_t temp = gb->cpu_reg.a - gb->cpu_reg.b;
				gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
				gb->cpu_reg.f_bits.n = 1;
				gb->cpu_reg.f_bits.h =
					(gb->cpu_reg.a ^ gb->cpu_reg.b ^ temp) & 0x10 ? 1 : 0;
				gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
				break;
			}
		case 0xB9: /* CP C */
			{
				uint16_t temp = gb->cpu_reg.a - gb->cpu_reg.c;
				gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
				gb->cpu_reg.f_bits.n = 1;
				gb->cpu_reg.f_bits.h =
					(gb->cpu_reg.a ^ gb->cpu_reg.c ^ temp) & 0x10 ? 1 : 0;
				gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
				break;
			}
		case 0xBA: /* CP D */
			{
				uint16_t temp = gb->cpu_reg.a - gb->cpu_reg.d;
				gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
				gb->cpu_reg.f_bits.n = 1;
				gb->cpu_reg.f_bits.h =
					(gb->cpu_reg.a ^ gb->cpu_reg.d ^ temp) & 0x10 ? 1 : 0;
				gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
				break;
			}
		case 0xBB: /* CP E */
			{
				uint16_t temp = gb->cpu_reg.a - gb->cpu_reg.e;
				gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
				gb->cpu_reg.f_bits.n = 1;
				gb->cpu_reg.f_bits.h =
					(gb->cpu_reg.a ^ gb->cpu_reg.e ^ temp) & 0x10 ? 1 : 0;
				gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
				break;
			}
		case 0xBC: /* CP H */
			{
				uint16_t temp = gb->cpu_reg.a - gb->cpu_reg.h;
				gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
				gb->cpu_reg.f_bits.n = 1;
				gb->cpu_reg.f_bits.h =
					(gb->cpu_reg.a ^ gb->cpu_reg.h ^ temp) & 0x10 ? 1 : 0;
				gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
				break;
			}
		case 0xBD: /* CP L */
			{
				uint16_t temp = gb->cpu_reg.a - gb->cpu_reg.l;
				gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
				gb->cpu_reg.f_bits.n = 1;
				gb->cpu_reg.f_bits.h =
					(gb->cpu_reg.a ^ gb->cpu_reg.l ^ temp) & 0x10 ? 1 : 0;
				gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
				break;
			}
			/* TODO: Optimsation by combining similar opcode routines. */
		case 0xBE: /* CP B */
			{
				uint8_t val = __gb_read(gb, gb->cpu_reg.hl);
				uint16_t temp = gb->cpu_reg.a - val;
				gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
				gb->cpu_reg.f_bits.n = 1;
				gb->cpu_reg.f_bits.h =
					(gb->cpu_reg.a ^ val ^ temp) & 0x10 ? 1 : 0;
				gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
				break;
			}
		case 0xBF: /* CP A */
			gb->cpu_reg.f_bits.z = 1;
			gb->cpu_reg.f_bits.n = 1;
			gb->cpu_reg.f_bits.h = 0;
			gb->cpu_reg.f_bits.c = 0;
			break;
		case 0xC0: /* RET NZ */
			if(!gb->cpu_reg.f_bits.z)
			{
				gb->cpu_reg.pc = __gb_read(gb, gb->cpu_reg.sp++);
				gb->cpu_reg.pc |= __gb_read(gb, gb->cpu_reg.sp++) << 8;
			}
			break;
		case 0xC1: /* POP BC */
			gb->cpu_reg.c = __gb_read(gb, gb->cpu_reg.sp++);
			gb->cpu_reg.b = __gb_read(gb, gb->cpu_reg.sp++);
			break;
		case 0xC2: /* JP NZ, imm */
			if(!gb->cpu_reg.f_bits.z)
			{
				uint16_t temp = __gb_read(gb, gb->cpu_reg.pc++);
				temp |= __gb_read(gb, gb->cpu_reg.pc++) << 8;
				gb->cpu_reg.pc = temp;
			}
			else
				gb->cpu_reg.pc += 2;

			break;
		case 0xC3: /* JP imm */
			{
				uint16_t temp = __gb_read(gb, gb->cpu_reg.pc++);
				temp |= __gb_read(gb, gb->cpu_reg.pc) << 8;
				gb->cpu_reg.pc = temp;
				break;
			}
		case 0xC4: /* CALL NZ imm */
			if(!gb->cpu_reg.f_bits.z)
			{
				uint16_t temp = __gb_read(gb, gb->cpu_reg.pc++);
				temp |= __gb_read(gb, gb->cpu_reg.pc++) << 8;
				__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
				__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);
				gb->cpu_reg.pc = temp;
			}
			else
				gb->cpu_reg.pc += 2;

			break;
		case 0xC5: /* PUSH BC */
			__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.b);
			__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.c);
			break;
		case 0xC6: /* ADD A, imm */
			{
				/* Taken from SameBoy, which is released under MIT Licence. */
				uint8_t value = __gb_read(gb, gb->cpu_reg.pc++);
				uint16_t calc = gb->cpu_reg.a + value;
				gb->cpu_reg.f_bits.z = ((uint8_t)calc == 0) ? 1 : 0;
				gb->cpu_reg.f_bits.h =
					((gb->cpu_reg.a & 0xF) + (value & 0xF) > 0x0F) ? 1 : 0;
				gb->cpu_reg.f_bits.c = calc > 0xFF ? 1 : 0;
				gb->cpu_reg.f_bits.n = 0;
				gb->cpu_reg.a = (uint8_t)calc;
				break;
			}
		case 0xC7: /* RST 0x0000 */
			__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
			__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);
			gb->cpu_reg.pc = 0x0000;
			break;
		case 0xC8: /* RET Z */
			if(gb->cpu_reg.f_bits.z)
			{
				uint16_t temp = __gb_read(gb, gb->cpu_reg.sp++);
				temp |= __gb_read(gb, gb->cpu_reg.sp++) << 8;
				gb->cpu_reg.pc = temp;
			}
			break;
		case 0xC9: /* RET */
			{
				uint16_t temp = __gb_read(gb, gb->cpu_reg.sp++);
				temp |= __gb_read(gb, gb->cpu_reg.sp++) << 8;
				gb->cpu_reg.pc = temp;
				break;
			}
		case 0xCA: /* JP Z, imm */
			if(gb->cpu_reg.f_bits.z)
			{
				uint16_t temp = __gb_read(gb, gb->cpu_reg.pc++);
				temp |= __gb_read(gb, gb->cpu_reg.pc++) << 8;
				gb->cpu_reg.pc = temp;
			}
			else
				gb->cpu_reg.pc += 2;
			break;
		case 0xCB: /* CB INST */
			__gb_execute_cb(gb);
			break;
		case 0xCC: /* CALL Z, imm */
			if(gb->cpu_reg.f_bits.z)
			{
				uint16_t temp = __gb_read(gb, gb->cpu_reg.pc++);
				temp |= __gb_read(gb, gb->cpu_reg.pc++) << 8;
				__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
				__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);
				gb->cpu_reg.pc = temp;
			}
			else
				gb->cpu_reg.pc += 2;
			break;
		case 0xCD: /* CALL imm */
			{
				uint16_t addr = __gb_read(gb, gb->cpu_reg.pc++);
				addr |= __gb_read(gb, gb->cpu_reg.pc++) << 8;
				__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
				__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);
				gb->cpu_reg.pc = addr;
			}
			break;
		case 0xCE: /* ADC A, imm */
			{
				uint8_t value, a, carry;
				value = __gb_read(gb, gb->cpu_reg.pc++);
				a = gb->cpu_reg.a;
				carry = gb->cpu_reg.f_bits.c;
				gb->cpu_reg.a = a + value + carry;

				gb->cpu_reg.f_bits.z = gb->cpu_reg.a == 0 ? 1 : 0;
				gb->cpu_reg.f_bits.h =
					((a & 0xF) + (value & 0xF) + carry > 0x0F) ? 1 : 0;
				gb->cpu_reg.f_bits.c =
					(((uint16_t) a) + ((uint16_t) value) + carry > 0xFF) ? 1 : 0;
				gb->cpu_reg.f_bits.n = 0;
				break;
			}
		case 0xCF: /* RST 0x0008 */
			__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
			__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);
			gb->cpu_reg.pc = 0x0008;
			break;
		case 0xD0: /* RET NC */
			if(!gb->cpu_reg.f_bits.c)
			{
				uint16_t temp = __gb_read(gb, gb->cpu_reg.sp++);
				temp |= __gb_read(gb, gb->cpu_reg.sp++) << 8;
				gb->cpu_reg.pc = temp;
			}
			break;
		case 0xD1: /* POP DE */
			gb->cpu_reg.e = __gb_read(gb, gb->cpu_reg.sp++);
			gb->cpu_reg.d = __gb_read(gb, gb->cpu_reg.sp++);
			break;
		case 0xD2: /* JP NC, imm */
			if(!gb->cpu_reg.f_bits.c)
			{
				uint16_t temp =  __gb_read(gb, gb->cpu_reg.pc++);
				temp |=  __gb_read(gb, gb->cpu_reg.pc++) << 8;
				gb->cpu_reg.pc = temp;
			}
			else
				gb->cpu_reg.pc += 2;
			break;
		case 0xD4: /* CALL NC, imm */
			if(!gb->cpu_reg.f_bits.c)
			{
				uint16_t temp = __gb_read(gb, gb->cpu_reg.pc++);
				temp |= __gb_read(gb, gb->cpu_reg.pc++) << 8;
				__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
				__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);
				gb->cpu_reg.pc = temp;
			}
			else
				gb->cpu_reg.pc += 2;
			break;
		case 0xD5: /* PUSH DE */
			__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.d);
			__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.e);
			break;
		case 0xD6: /* SUB imm */
			{
				uint8_t val = __gb_read(gb, gb->cpu_reg.pc++);
				uint16_t temp = gb->cpu_reg.a - val;
				gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
				gb->cpu_reg.f_bits.n = 1;
				gb->cpu_reg.f_bits.h =
					(gb->cpu_reg.a ^ val ^ temp) & 0x10 ? 1 : 0;
				gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
				gb->cpu_reg.a = (temp & 0xFF);
				break;
			}
		case 0xD7: /* RST 0x0010 */
			__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
			__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);
			gb->cpu_reg.pc = 0x0010;
			break;
		case 0xD8: /* RET C */
			if(gb->cpu_reg.f_bits.c)
			{
				uint16_t temp = __gb_read(gb, gb->cpu_reg.sp++);
				temp |= __gb_read(gb, gb->cpu_reg.sp++) << 8;
				gb->cpu_reg.pc = temp;
			}
			break;
		case 0xD9: /* RETI */
			{
				uint16_t temp = __gb_read(gb, gb->cpu_reg.sp++);
				temp |= __gb_read(gb, gb->cpu_reg.sp++) << 8;
				gb->cpu_reg.pc = temp;
				gb->gb_ime = 1;
			}
			break;
		case 0xDA: /* JP C, imm */
			if(gb->cpu_reg.f_bits.c)
			{
				uint16_t addr = __gb_read(gb, gb->cpu_reg.pc++);
				addr |= __gb_read(gb, gb->cpu_reg.pc++) << 8;
				gb->cpu_reg.pc = addr;
			}
			else
				gb->cpu_reg.pc += 2;
			break;
		case 0xDC: /* CALL C, imm */
			if(gb->cpu_reg.f_bits.c)
			{
				uint16_t temp = __gb_read(gb, gb->cpu_reg.pc++);
				temp |= __gb_read(gb, gb->cpu_reg.pc++) << 8;
				__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
				__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);
				gb->cpu_reg.pc = temp;
			}
			else
				gb->cpu_reg.pc += 2;
			break;
		case 0xDE: /* SBC A, imm */
			{
				uint8_t temp_8 = __gb_read(gb, gb->cpu_reg.pc++);
				uint16_t temp_16 = gb->cpu_reg.a - temp_8 - gb->cpu_reg.f_bits.c;
				gb->cpu_reg.f_bits.z = ((temp_16 & 0xFF) == 0x00);
				gb->cpu_reg.f_bits.n = 1;
				gb->cpu_reg.f_bits.h =
					(gb->cpu_reg.a ^ temp_8 ^ temp_16) & 0x10 ? 1 : 0;
				gb->cpu_reg.f_bits.c = (temp_16 & 0xFF00) ? 1 : 0;
				gb->cpu_reg.a = (temp_16 & 0xFF);
				break;
			}
		case 0xDF: /* RST 0x0018 */
			__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
			__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);
			gb->cpu_reg.pc = 0x0018;
			break;
		case 0xE0: /* LD (0xFF00+imm), A */
			__gb_write(gb, 0xFF00 | __gb_read(gb, gb->cpu_reg.pc++),
					gb->cpu_reg.a);
			break;
		case 0xE1: /* POP HL */
			gb->cpu_reg.l = __gb_read(gb, gb->cpu_reg.sp++);
			gb->cpu_reg.h = __gb_read(gb, gb->cpu_reg.sp++);
			break;
		case 0xE2: /* LD (C), A */
			__gb_write(gb, 0xFF00 | gb->cpu_reg.c, gb->cpu_reg.a);
			break;
		case 0xE5: /* PUSH HL */
			__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.h);
			__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.l);
			break;
		case 0xE6: /* AND imm */
			/* TODO: Optimisation? */
			gb->cpu_reg.a = gb->cpu_reg.a & __gb_read(gb, gb->cpu_reg.pc++);
			gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
			gb->cpu_reg.f_bits.n = 0;
			gb->cpu_reg.f_bits.h = 1;
			gb->cpu_reg.f_bits.c = 0;
			break;
		case 0xE7: /* RST 0x0020 */
			__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
			__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);
			gb->cpu_reg.pc = 0x0020;
			break;
		case 0xE8: /* ADD SP, imm */
			{
				int8_t offset = (int8_t) __gb_read(gb, gb->cpu_reg.pc++);
				/* TODO: Move flag assignments for optimisation. */
				gb->cpu_reg.f_bits.z = 0;
				gb->cpu_reg.f_bits.n = 0;
				gb->cpu_reg.f_bits.h = ((gb->cpu_reg.sp & 0xF) + (offset & 0xF) > 0xF) ? 1 : 0;
				gb->cpu_reg.f_bits.c = ((gb->cpu_reg.sp & 0xFF) + (offset & 0xFF) > 0xFF);
				gb->cpu_reg.sp += offset;
				break;
			}
		case 0xE9: /* JP (HL) */
			gb->cpu_reg.pc = gb->cpu_reg.hl;
			break;
		case 0xEA: /* LD (imm), A */
			{
				uint16_t addr = __gb_read(gb, gb->cpu_reg.pc++);
				addr |= __gb_read(gb, gb->cpu_reg.pc++) << 8;
				__gb_write(gb, addr, gb->cpu_reg.a);
				break;
			}
		case 0xEE: /* XOR imm */
			gb->cpu_reg.a = gb->cpu_reg.a ^ __gb_read(gb, gb->cpu_reg.pc++);
			gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
			gb->cpu_reg.f_bits.n = 0;
			gb->cpu_reg.f_bits.h = 0;
			gb->cpu_reg.f_bits.c = 0;
			break;
		case 0xEF: /* RST 0x0028 */
			__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
			__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);
			gb->cpu_reg.pc = 0x0028;
			break;
		case 0xF0: /* LD A, (0xFF00+imm) */
			gb->cpu_reg.a =
				__gb_read(gb, 0xFF00 | __gb_read(gb, gb->cpu_reg.pc++));
			break;
		case 0xF1: /* POP AF */
			{
				uint8_t temp_8 = __gb_read(gb, gb->cpu_reg.sp++);
				gb->cpu_reg.f_bits.z = (temp_8 >> 7) & 1;
				gb->cpu_reg.f_bits.n = (temp_8 >> 6) & 1;
				gb->cpu_reg.f_bits.h = (temp_8 >> 5) & 1;
				gb->cpu_reg.f_bits.c = (temp_8 >> 4) & 1;
				gb->cpu_reg.a = __gb_read(gb, gb->cpu_reg.sp++);
				break;
			}
		case 0xF2: /* LD A, (C) */
			gb->cpu_reg.a = __gb_read(gb, 0xFF00 | gb->cpu_reg.c);
			break;
		case 0xF3: /* DI */
			gb->gb_ime = 0;
			break;
		case 0xF5: /* PUSH AF */
			__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.a);
			__gb_write(gb, --gb->cpu_reg.sp,
					gb->cpu_reg.f_bits.z << 7 | gb->cpu_reg.f_bits.n << 6 |
					gb->cpu_reg.f_bits.h << 5 | gb->cpu_reg.f_bits.c << 4);
			break;
		case 0xF6: /* OR imm */
			gb->cpu_reg.a = gb->cpu_reg.a | __gb_read(gb, gb->cpu_reg.pc++);
			gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
			gb->cpu_reg.f_bits.n = 0;
			gb->cpu_reg.f_bits.h = 0;
			gb->cpu_reg.f_bits.c = 0;
			break;
		case 0xF7: /* PUSH AF */
			__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
			__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);
			gb->cpu_reg.pc = 0x0030;
			break;
		case 0xF8: /* LD HL, SP+/-imm */
			{
				/* Taken from SameBoy, which is released under MIT Licence. */
				int8_t offset = (int8_t) __gb_read(gb, gb->cpu_reg.pc++);
				gb->cpu_reg.hl = gb->cpu_reg.sp + offset;
				gb->cpu_reg.f_bits.z = 0;
				gb->cpu_reg.f_bits.n = 0;
				gb->cpu_reg.f_bits.h = ((gb->cpu_reg.sp & 0xF) + (offset & 0xF) > 0xF) ? 1 : 0;
				gb->cpu_reg.f_bits.c = ((gb->cpu_reg.sp & 0xFF) + (offset & 0xFF) > 0xFF) ? 1 : 0;
				break;
			}
		case 0xF9: /* LD SP, HL */
			gb->cpu_reg.sp = gb->cpu_reg.hl;
			break;
		case 0xFA: /* LD A, (imm) */
			{
				uint16_t addr = __gb_read(gb, gb->cpu_reg.pc++);
				addr |= __gb_read(gb, gb->cpu_reg.pc++) << 8;
				gb->cpu_reg.a = __gb_read(gb, addr);
				break;
			}
		case 0xFB: /* EI */
			gb->gb_ime = 1;
			break;
		case 0xFE: /* CP imm */
			{
				uint8_t temp_8 = __gb_read(gb, gb->cpu_reg.pc++);
				uint16_t temp_16 = gb->cpu_reg.a - temp_8;
				gb->cpu_reg.f_bits.z = ((temp_16 & 0xFF) == 0x00);
				gb->cpu_reg.f_bits.n = 1;
				gb->cpu_reg.f_bits.h = ((gb->cpu_reg.a ^ temp_8 ^ temp_16) & 0x10) ? 1 : 0;
				gb->cpu_reg.f_bits.c = (temp_16 & 0xFF00) ? 1 : 0;
				break;
			}
		case 0xFF: /* RST 0x0038 */
			__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
			__gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);
			gb->cpu_reg.pc = 0x0038;
			break;

		default:
			gb->gb_error(gb, GB_INVALID_OPCODE, opcode);
	}

	/* CPU Timing */
	gb->timer.cpu_count += inst_cycles;

	/* DIV register timing */
	gb->timer.div_count += inst_cycles;

	if(gb->timer.div_count > DIV_CYCLES)
	{
		gb->gb_reg.DIV++;
		gb->timer.div_count -= DIV_CYCLES;
	}

	/* TIMA register timing */
	if(gb->timer.tac_enable)
	{
		static const unsigned int TAC_CYCLES[4] = {1024, 16, 64, 256};

		gb->timer.tima_count += inst_cycles;
		if(gb->timer.tima_count > TAC_CYCLES[gb->timer.tac_rate])
		{
			gb->timer.tima_count -= TAC_CYCLES[gb->timer.tac_rate];
			gb->gb_reg.TIMA++;
			if(gb->gb_reg.TIMA == 0)
			{
				gb->gb_reg.IF |= TIMER_INTR;
				gb->gb_reg.TIMA = gb->gb_reg.TMA;
			}
		}
	}

	/* TODO Check behaviour of LCD during LCD power off state. */
	/* If LCD is off, don't update LCD state. */
	if((gb->gb_reg.LCDC & LCDC_ENABLE) == 0)
		return;

	/* LCD Timing */
	gb->timer.lcd_count += inst_cycles;

	/* New Scanline */
	if(gb->timer.lcd_count > LCD_LINE_CYCLES)
	{
		gb->timer.lcd_count -= LCD_LINE_CYCLES;

		/* LYC Update */
		if(gb->gb_reg.LY == gb->gb_reg.LYC)
		{
			gb->gb_reg.STAT |= STAT_LYC_COINC;
			if(gb->gb_reg.STAT & STAT_LYC_INTR)
				gb->gb_reg.IF |= LCDC_INTR;
		}
		else
			gb->gb_reg.STAT &= 0xFB;

		/* Next line */
		gb->gb_reg.LY = (gb->gb_reg.LY + 1) % LCD_VERT_LINES;

		/* VBLANK Start */
		if(gb->gb_reg.LY == LCD_HEIGHT)
		{
			gb->lcd_mode = LCD_VBLANK;
			gb->gb_frame = 1;
			gb->gb_reg.IF |= VBLANK_INTR;
			if(gb->gb_reg.STAT & STAT_MODE_1_INTR)
				gb->gb_reg.IF |= LCDC_INTR;
		}
		/* Normal Line */
		else if(gb->gb_reg.LY < LCD_HEIGHT)
		{
			if(gb->gb_reg.LY == 0)
			{
				/* Clear Screen */
				gb->WY = gb->gb_reg.WY;
				gb->WYC = 0;
				memset(gb->gb_fb, 0x00, LCD_WIDTH * LCD_HEIGHT);
			}

			gb->lcd_mode = LCD_HBLANK;
			if(gb->gb_reg.STAT & STAT_MODE_0_INTR)
				gb->gb_reg.IF |= LCDC_INTR;
		}
	}
	/* OAM access */
	else if(gb->lcd_mode == LCD_HBLANK && gb->timer.lcd_count >= LCD_MODE_2_CYCLES)
	{
		gb->lcd_mode = LCD_SEARCH_OAM;
		if(gb->gb_reg.STAT & STAT_MODE_2_INTR)
			gb->gb_reg.IF |= LCDC_INTR;
	}
	/* Update LCD */
	else if(gb->lcd_mode == LCD_SEARCH_OAM && gb->timer.lcd_count >= LCD_MODE_3_CYCLES)
	{
		gb->lcd_mode = LCD_TRANSFER;
		/* TODO: LCD_DRAW_LINE(); */
#if ENABLE_LCD
		__gb_draw_line(gb);
#endif
	}
}

void gb_run_frame(struct gb_t *gb)
{
	gb->gb_frame = 0;
	while(!gb->gb_frame)
		__gb_step_cpu(gb);
}

/**
 * Gets the size of the save file required for the ROM.
 */
uint32_t gb_get_save_size(struct gb_t *gb)
{
	const unsigned int ram_size_location = 0x0149;
	const uint32_t ram_sizes[] = {
		0x00, 0x800, 0x2000, 0x8000, 0x20000
	};
	uint32_t ram_size = gb->gb_rom_read(gb, ram_size_location);
	return ram_sizes[ram_size];
}
/**
 * Initialise the emulator context.
 */
enum gb_init_error_e gb_init(struct gb_t *gb,
		uint8_t (*gb_rom_read)(struct gb_t*, const uint32_t),
		uint8_t (*gb_cart_ram_read)(struct gb_t*, const uint32_t),
		void (*gb_cart_ram_write)(struct gb_t*, const uint32_t, const uint8_t),
		void (*gb_error)(struct gb_t*, const enum gb_error_e, const uint16_t),
		void *priv)
{
	const uint16_t mbc_location = 0x0147;
	const uint16_t bank_count_location = 0x0148;
	const uint16_t ram_size_location = 0x0149;
	/**
	 * Table for cartridge type (MBC). -1 if invalid.
	 * TODO: MMM01 is untested.
	 * TODO: MBC6 is untested.
	 * TODO: MBC7 is unsupported.
	 * TODO: POCKET CAMERA is unsupported.
	 * TODO: BANDAI TAMA5 is unsupported.
	 * TODO: HuC3 is unsupported.
	 * TODO: HuC1 is unsupported.
	 **/
	const uint8_t cart_mbc[] = {
		0, 1, 1, 1,-1, 2, 2,-1, 0, 0,-1, 0, 0, 0,-1, 3,
		3, 3, 3, 3,-1,-1,-1,-1,-1, 5, 5, 5, 5, 5, 5,-1
	};
	const uint8_t cart_ram[] = {
		0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0,
		1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0
	};
	const uint16_t num_rom_banks[] = {
		2, 4, 8,16,32,64,128,256,512,0,0,0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0,72,80,96, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	};
	const uint8_t num_ram_banks[] = { 0, 1, 1, 4, 16, 8 };

	gb->gb_rom_read = gb_rom_read;
	gb->gb_cart_ram_read = gb_cart_ram_read;
	gb->gb_cart_ram_write = gb_cart_ram_write;
	gb->gb_error = gb_error;
	gb->priv = priv;

	/* Check if cartridge type is supported, and set MBC type. */
	{
		const uint8_t mbc_value = gb->gb_rom_read(gb, mbc_location);
		if(mbc_value > sizeof(cart_mbc) - 1 ||
				(gb->mbc = cart_mbc[gb->gb_rom_read(gb, mbc_location)]) == 255u)
			return GB_INIT_CARTRIDGE_UNSUPPORTED;
	}

	gb->cart_ram = cart_ram[gb->gb_rom_read(gb, mbc_location)];
	gb->num_rom_banks = num_rom_banks[gb->gb_rom_read(gb, bank_count_location)];
	gb->num_ram_banks = num_ram_banks[gb->gb_rom_read(gb, ram_size_location)];

	__gb_reset(gb);

	return GB_INIT_NO_ERROR;
}
