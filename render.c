#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

typedef unsigned char uint8_t;
typedef signed char int8_t;
typedef unsigned short uint16_t;

static uint16_t lfsr = 0xace1u;

#define PLAIN_TILES 0
#define V_TILES 6
#define H_TILES 12
#define WRAP_TILES 18
#define FIRST_NONCOLOUR 24
#define SWIRL_TILE 24
#define CAGE_TILE 25
#define COLOURBOMB_TILE 26
#define EXPLOSION_TILE 27
#define BG_TILES 28

#define DIGITS_TEXT 32
#define JELLY_TEXT 33
#define SCORE_TEXT 34
#define MOVES_TEXT 35
#define LEVEL1_PTR 36

#define SWIRL_MASK 0x80
#define CAGE_MASK  0x40
#define BG_MASK    0x3f

#define EMPTY_TILE 31

//#define CHEATMODE 1
#define ROM

static uint8_t *const screenbase = (uint8_t *) 0x4100;

#ifdef TILES_LINKED_IN
extern uint8_t *tiles[];
#elif defined(ROM)
uint8_t **tiles = (uint8_t **) 0xe00;
#else
uint8_t **tiles = (uint8_t **) 0x8000;
#endif

#define ROWLENGTH 576

#define READ_BYTE(A) (*(volatile uint8_t *) (A))
#define WRITE_BYTE(A, V) (*(volatile uint8_t *) (A) = (V))

#ifndef TILES_LINKED_IN
static uint8_t oldbank;

#define CENTRE(N) ((36 - (N) * 4) * 8)

static void
select_sram (uint8_t newbank)
{
  oldbank = READ_BYTE (0xf4);
  WRITE_BYTE (0xf4, newbank);
  WRITE_BYTE (0xfe30, newbank);
}

static void
deselect_sram (void)
{
  WRITE_BYTE (0xf4, oldbank);
  WRITE_BYTE (0xfe30, oldbank);
}
#endif

static unsigned int
rand (void)
{
  uint16_t bit;        /* Must be 16bit to allow bit<<15 later in the code */

  /* taps: 16 14 13 11; feedback polynomial: x^16 + x^14 + x^13 + x^11 + 1 */
  bit  = ((lfsr >> 0) ^ (lfsr >> 2) ^ (lfsr >> 3) ^ (lfsr >> 5) ) & 1;
  lfsr =  (lfsr >> 1) | (bit << 15);

  return lfsr;
}

static void
oswrch (uint8_t x)
{
  __asm__ __volatile__ ("jsr $ffee" : : "Aq" (x));
}

static uint8_t
osbyte (uint8_t a, uint8_t x, uint8_t y)
{
  unsigned char dma, dmx, dmy;
  __asm__ __volatile__ ("jsr $fff4" : "=Aq" (dma), "=xq" (dmx), "=yq" (dmy)
				    : "Aq" (a), "xq" (x), "yq" (y));
  return dma;
}

static int
osrdch (void)
{
  unsigned char ra, rx;

  __asm__ __volatile__ (
    "jsr $ffe0\n\t"
    "ldx #0\n\t"
    "bcc :+\n\t"
    "ldx #1\n"
    ":\t" : "=Aq" (ra), "=xq" (rx));

  if (rx)
    {
      osbyte (126, 0, 0);
      return -1;
    }

  return ra;
}

static void
osword (unsigned char code, void *parameters)
{
  unsigned char addr_lo = ((unsigned short) parameters) & 0xff;
  unsigned char addr_hi = (((unsigned short) parameters) >> 8) & 0xff;
  unsigned char dma, dmx, dmy;
  __asm__ __volatile__ ("jsr $fff1"
			: "=Aq" (dma), "=xq" (dmx), "=yq" (dmy)
			: "Aq" (code), "xq" (addr_lo), "yq" (addr_hi)
			: "memory");
}

static void
oscli (unsigned char *cmd)
{
  unsigned char cmd_lo = ((unsigned short) cmd) & 0xff;
  unsigned char cmd_hi = (((unsigned short) cmd) >> 8) & 0xff;
  unsigned char dma, dmx, dmy;
  __asm__ __volatile__ ("jsr $fff7"
			: "=Aq" (dma), "=xq" (dmx), "=yq" (dmy)
			: "xq" (cmd_lo), "yq" (cmd_hi)
			: "memory");
}

static unsigned char osfile_params[18];

static unsigned char
osfile (unsigned char code)
{
  unsigned char addr_lo = ((unsigned short) osfile_params) & 0xff;
  unsigned char addr_hi = (((unsigned short) osfile_params) >> 8) & 0xff;
  unsigned char aout;
  __asm__ __volatile__ ("jsr $ffdd"
			: "=Aq" (aout)
			: "Aq" (code), "xq" (addr_lo), "yq" (addr_hi)
			: "memory");
  return aout;
}

static void
osfile_load (const char *filename, void *address)
{
  osfile_params[0] = ((unsigned short) filename) & 0xff;
  osfile_params[1] = (((unsigned short) filename) >> 8) & 0xff;
  osfile_params[2] = ((unsigned short) address) & 0xff;
  osfile_params[3] = (((unsigned short) address) >> 8) & 0xff;
  osfile_params[4] = osfile_params[5] = 0;
  osfile_params[6] = osfile_params[7] = osfile_params[8] = osfile_params[9] = 0;
  osfile (255);
}

static void
setmode (uint8_t mode)
{
  oswrch (22);
  oswrch (mode);
}

static void
setpalette (uint8_t physical, uint8_t logical)
{
  static uint8_t params[5] = {0, 0, 0, 0, 0};
  params[0] = physical;
  params[1] = logical;
  osword (0xc, &params[0]);
}

static void
gfx_plot (uint8_t code, unsigned x, unsigned y)
{
  oswrch (25);
  oswrch (code);
  oswrch (x & 255);
  oswrch (x >> 8);
  oswrch (y & 255);
  oswrch (y >> 8);
}

static void
gfx_gcol (unsigned op, unsigned col)
{
  oswrch (18);
  oswrch (op);
  oswrch (col);
}

static void
tweak_gfx (unsigned x, unsigned y)
{
  unsigned addr = (unsigned) &screenbase[(y>>3) * ROWLENGTH + (x >> 1) * 8];
  WRITE_BYTE (0xd6, addr & 255);
  WRITE_BYTE (0xd7, (addr >> 8));
}

static void
gfx_move (unsigned x, unsigned y)
{
  tweak_gfx (x, y);
  gfx_plot (4, x, y);
}

static void
gfx_draw (unsigned x, unsigned y)
{
  tweak_gfx (x, y);
  gfx_plot (5, x, y);
}

static void
vdu_var (uint8_t reg, uint8_t val)
{
  uint8_t n;
  oswrch(23);
  oswrch(0);
  oswrch(reg);
  oswrch(val);
  for (n = 0; n < 6; n++)
    oswrch (0);
}

static void
screen_start (void *addr)
{
  unsigned int iaddr = (unsigned int) addr;
  iaddr >>= 3;
  vdu_var (13, iaddr & 255);
  vdu_var (12, iaddr >> 8);
}


static void
render_tile (uint8_t *addr, uint8_t tileno)
{
  uint8_t x, y, row;
  uint8_t *tileptr = tiles[tileno];
  uint8_t count = 0, type = 0;

  for (x = 0; x < 8; x++)
    {
      uint8_t *coladdr = addr;

      for (row = 0; row < 3; row++)
        {
          uint8_t *rowaddr = coladdr;

          for (y = 0; y < 8; y++)
            {
              if (count == 0)
                {
                  uint8_t byte = *tileptr++;
                  if ((byte & 0xc0) == 0)
                    type = 0;
                  else if ((byte & 0xc0) == 0x40) /* solid */
                    type = 1;
                  else if ((byte & 0xc0) == 0x80) /* lpix only */
                    type = 2;
                  else if ((byte & 0xc0) == 0xc0) /* rpix only */
                    type = 3;
                  count = byte & 0x3f;
                }

              if (type == 1)
                *rowaddr = *tileptr++;
              else if (type == 2)
                {
                  uint8_t screenbyte = *rowaddr;
                  screenbyte &= 0x55;
                  screenbyte |= *tileptr++;
                  *rowaddr = screenbyte;
                }
              else if (type == 3)
                {
                  uint8_t screenbyte = *rowaddr;
                  screenbyte &= 0xaa;
                  screenbyte |= *tileptr++;
                  *rowaddr = screenbyte;
                }

              count--;
              rowaddr++;
            }
          
          coladdr += ROWLENGTH;
        }

      addr += 8;
    }
}

static void
render_solid_tile (uint8_t *addr, uint8_t tileno)
{
  uint8_t x, y, row;
  uint8_t *tileptr = tiles[tileno];

  for (x = 0; x < 8; x++)
    {
      uint8_t *coladdr = addr;

      for (row = 0; row < 3; row++)
        {
          memcpy (coladdr, tileptr, 8);
          tileptr += 8;
          coladdr += ROWLENGTH;
        }

      addr += 8;
    }
}

static uint8_t rng (void)
{
  uint8_t rnum;
  
  do {
    rnum = rand() & 7;
  } while (rnum > 5);
  
  return rnum;
}

static uint8_t playfield[9][9];

#if 1
static uint8_t background[9][9] =
  {
    { 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 1, 0, 0, 0, 0 },
    { 0, 0, 0, 1, 1, 1, 0, 0, 0 },
    { 0, 0, 0, 0, 1, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0 }
  };
#else

#define S SWIRL_MASK
#define C CAGE_MASK

#if 0

static uint8_t background[9][9] =
  {
    { 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, S, S, S, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { C, 0, 0, 0, 0, 0, 0, 0, C },
    { 1|C, 0, 0, 0, 0, 0, 0, 0, 1|C },
    { 2|C, 2|C, 0, 0, 0, 0, 0, 2|C, 2|C }
  };

#else

static uint8_t background[9][9] =
  {
    { 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { S, S, S, S, S, S, S, S, S },
    { C, C, C, C, C, C, C, C, C },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 1, 1, 1, 1, 1, 1, 1, 1, 1 },
    { 1, 1, 1, 1, 1, 1, 1, 1, 1 }
  };

#endif

#undef C
#undef H

#endif

static unsigned long thescore = 0;
static unsigned movesleft = 100;
static uint8_t jellies;

static void
hline (uint8_t sx, uint8_t ex, uint8_t y, uint8_t andcol, uint8_t orcol)
{
  uint8_t *row = &screenbase[(y >> 3) * ROWLENGTH + (y & 7)];
  uint8_t x, len = ex - sx;

  if (sx & 1)
    {
      row[(sx & ~1) << 2] = (row[(sx & ~1) << 2] & (andcol | 0xaa))
                            | (orcol & 0x55);
      sx++;
      len--;
    }

  if (len > 2)
    for (x = sx; x < ex - 1; x += 2)
      row[x << 2] = (row[x << 2] & andcol) | orcol;

  if (ex & 1)
    row[(ex & ~1) << 2] = (row[(ex & ~1) << 2] & (andcol | 0x55))
                          | (orcol & 0xaa);
}

static void
vline (uint8_t x, uint8_t sy, uint8_t ey, uint8_t andcol, uint8_t orcol)
{
  uint8_t *row = &screenbase[(sy >> 3) * ROWLENGTH + ((x & ~1) << 2)];
  uint8_t y;

  if (x & 1)
    {
      andcol |= 0xaa;
      orcol &= 0x55;
    }
  else
    {
      andcol |= 0x55;
      orcol &= 0xaa;
    }

  for (y = sy; y <= ey; y++)
    {
      row[y & 7] = (row[y & 7] & andcol) | orcol;
      if ((y & 7) == 7)
        row += ROWLENGTH;
    }
}

static void box (uint8_t cursx, uint8_t cursy, uint8_t andcol, uint8_t orcol)
{
  unsigned left = cursx * 16;
  unsigned bottom = cursy * 24;
  hline (left, left + 15, bottom, andcol, orcol);
  hline (left, left + 15, bottom + 23, andcol, orcol);
  vline (left, bottom, bottom + 23, andcol, orcol);
  vline (left + 15, bottom, bottom + 23, andcol, orcol);
  /*unsigned left = cursx * 128;
  unsigned bottom = 928 - cursy * 96;
  gfx_move (left, bottom);
  gfx_draw (left + 120, bottom);
  gfx_draw (left + 120, bottom + 92);
  gfx_draw (left, bottom + 92);
  gfx_draw (left, bottom);*/
}

static void
redraw_tile (uint8_t x, uint8_t y)
{
  uint8_t *tileat = &screenbase[y * ROWLENGTH * 3 + x * 8 * 8];
  render_solid_tile (tileat, BG_TILES + (background[y][x] & BG_MASK));
  if ((playfield[y][x] & 127) != EMPTY_TILE)
    render_tile (tileat, playfield[y][x] & 127);
  if (background[y][x] & CAGE_MASK)
    render_tile (tileat, CAGE_TILE);
}

static void
render_tile_xy (uint8_t x, uint8_t y, uint8_t tileno)
{
  uint8_t *tileat = &screenbase[y * ROWLENGTH * 3 + x * 8 * 8];
  render_tile (tileat, tileno);
}

static uint8_t
candy_match (uint8_t lhs, uint8_t rhs)
{
  lhs &= 127;
  rhs &= 127;

  if (lhs < FIRST_NONCOLOUR && rhs < FIRST_NONCOLOUR)
    return (lhs % 6) == (rhs % 6);

  return 0;
}

static void
explode_a_colour (uint8_t c)
{
  uint8_t x, y;
  for (y = 0; y < 9; y++)
    for (x = 0; x < 9; x++)
      {
        if (candy_match (playfield[y][x], c))
          playfield[y][x] |= 128;
      }
}

static void trigger (uint8_t, uint8_t, uint8_t);

static uint8_t
stripes_match (uint8_t oldx, uint8_t oldy, uint8_t newx, uint8_t newy, uint8_t fix_move)
{
  uint8_t lhs = playfield[oldy][oldx], rhs = playfield[newy][newx];
  uint8_t i;

  if (lhs >= V_TILES && lhs < FIRST_NONCOLOUR
      && rhs >= V_TILES && rhs < FIRST_NONCOLOUR)
    {
      if (fix_move)
        for (i = 0; i < 9; i++)
          {
            trigger (i, oldy, lhs);
            trigger (oldx, i, lhs);
            trigger (i, newy, rhs);
            trigger (newx, i, rhs);
          }
      thescore += 3;
      return 1;
    }

  return 0;
}

static uint8_t
colourbomb_match (uint8_t *lhsp, uint8_t *rhsp, uint8_t fix_move)
{
  uint8_t lhs = *lhsp, rhs = *rhsp;

  if (lhs == COLOURBOMB_TILE)
    {
      if (fix_move && rhs < FIRST_NONCOLOUR)
        {
          *lhsp |= 128;
          explode_a_colour (rhs);
        }

      thescore += 3;

      return 1;
    }
  else if (rhs == COLOURBOMB_TILE)
    {
      if (fix_move && lhs < FIRST_NONCOLOUR)
        {
          *rhsp |= 128;
          explode_a_colour (lhs);
        }

      thescore += 3;

      return 1;
    }

  return 0;
}

// Terrifyingly recursive!

static void
trigger (uint8_t x, uint8_t y, uint8_t eq)
{
  uint8_t trigger_char = playfield[y][x] & 127, i, j;

  playfield[y][x] |= 128;
  thescore++;

  if (trigger_char == EMPTY_TILE || trigger_char == SWIRL_TILE)
    return;

  // We're exploding a colourbomb!
  if (trigger_char == COLOURBOMB_TILE)
    {
      explode_a_colour (eq);
      return;
    }

  if (trigger_char >= (uint8_t) H_TILES &&
      trigger_char < (uint8_t) (H_TILES + 6))
    for (i = 0; i < 9; i++)
      {
        if (!(playfield[y][i] & 128))
          trigger (i, y, eq);
      }

  if (trigger_char >= (uint8_t) V_TILES &&
      trigger_char < (uint8_t) (V_TILES + 6))
    for (i = 0; i < 9; i++)
      {
        if (!(playfield[i][x] & 128))
          trigger (x, i, eq);
      }

  if (trigger_char >= (uint8_t) WRAP_TILES
      && trigger_char < (uint8_t) (WRAP_TILES + 6))
    {
      uint8_t l = x > 0 ? x - 1 : 0;
      uint8_t t = y > 0 ? y - 1 : 0;
      uint8_t r = x < 8 ? x + 1 : 8;
      uint8_t b = y < 8 ? y + 1 : 8;
      for (i = l; i <= r; i++)
        for (j = t; j <= b; j++)
          {
            if (!(playfield[j][i] & 128))
              trigger (i, j, eq);
          }
    }
}

static uint8_t
horizontal_match (uint8_t x, uint8_t y, uint8_t eq, uint8_t fix_matches)
{
  uint8_t c;
  uint8_t right = x, left = x, matching;
  
  for (c = x + 1; c < 9; c++)
    {
      if (candy_match (playfield[y][c], eq))
        right = c;
      else
        break;
    }

  for (c = x - 1; c != 255; c--)
    {
      if (candy_match (playfield[y][c], eq))
        left = c;
      else
        break;
    }

  matching = right - left + 1;

  if (fix_matches && matching >= 3)
    {
      for (x = left; x <= right; x++)
        trigger (x, y, eq);
    }

  return matching;
}

static uint8_t
vertical_match (uint8_t x, uint8_t y, uint8_t eq, uint8_t fix_matches)
{
  uint8_t c;
  uint8_t bottom = y, top = y, matching;
  
  for (c = y + 1; c < 9; c++)
    {
      if (candy_match (playfield[c][x], eq))
        bottom = c;
      else
        break;
    }

  for (c = y - 1; c != 255; c--)
    {
      if (candy_match (playfield[c][x], eq))
        top = c;
      else
        break;
    }

  matching = bottom - top + 1;

  if (fix_matches && matching >= 3)
    {
      for (y = top; y <= bottom; y++)
        trigger (x, y, eq);
    }

  return matching;
}

static void
reset_playfield_marks (void)
{
  uint8_t x, y;
  for (y = 0; y < 9; y++)
    for (x = 0; x < 9; x++)
      playfield[y][x] &= ~128;
}

static void
do_swap (uint8_t oldx, uint8_t oldy, uint8_t newx, uint8_t newy)
{
  uint8_t oldtile = playfield[oldy][oldx];
  playfield[oldy][oldx] = playfield[newy][newx];
  playfield[newy][newx] = oldtile;
}

static void
show_swap (uint8_t oldx, uint8_t oldy, uint8_t newx, uint8_t newy)
{
  redraw_tile (oldx, oldy);
  redraw_tile (newx, newy);
}

static void sound (int channel, int amplitude, int pitch, int duration);

static void
show_explosions (void)
{
  uint8_t x, y;
  uint8_t made_jelly_sound = 0;
  uint8_t num_explosions = 0;
  uint8_t explosion_vol;

  for (y = 0; y < 9; y++)
    for (x = 0; x < 9; x++)
      {
        if (playfield[y][x] & 128)
          {
            if (!made_jelly_sound && (background[y][x] & BG_MASK) == 1)
              {
                sound (0x13, 3, 130 - jellies, 10);
                made_jelly_sound = 1;
              }

            render_tile_xy (x, y, EXPLOSION_TILE);
            num_explosions++;
          }
      }

  explosion_vol = (unsigned) num_explosions + 6;
  if (explosion_vol > 15)
    explosion_vol = 15;

  sound (0x10, -explosion_vol, 7, 15);
  sound (0x11, 2, 200, 15);
}

static void pause (unsigned long amt)
{
  unsigned long wait;
  for (wait = 0; wait < amt; wait++)
    __asm__ __volatile__ ("nop");
}

static void
deswirl (uint8_t x, uint8_t y)
{
  background[y][x] &= ~SWIRL_MASK;
  playfield[y][x] = EMPTY_TILE;
  redraw_tile (x, y);
  thescore += 10;
}

static void
shuffle_explosions (void)
{
  uint8_t x, y, y2;
  uint8_t some_explosions = 0;
  uint8_t some_movement = 0;

  for (y = 0; y < 9; y++)
    for (x = 0; x < 9; x++)
      {
        if (playfield[y][x] & 128)
          {
            playfield[y][x] = EMPTY_TILE;

            if (background[y][x] & CAGE_MASK)
              {
                background[y][x] &= ~CAGE_MASK;
                redraw_tile (x, y);
                thescore += 20;
              }

            if (x > 0 && (background[y][x - 1] & SWIRL_MASK))
              deswirl (x - 1, y);
            if (x < 8 && (background[y][x + 1] & SWIRL_MASK))
              deswirl (x + 1, y);
            if (y > 0 && (background[y - 1][x] & SWIRL_MASK))
              deswirl (x, y - 1);
            if (y < 8 && (background[y + 1][x] & SWIRL_MASK))
              deswirl (x, y + 1);

            // Remove jelly (like a boss).
            if (background[y][x] > 0 && background[y][x] <= 2)
              {
                thescore += 10;
                background[y][x]--;
              }
          }
      }

  do
    {
      some_explosions = some_movement = 0;
      for (y = 0; y < 9; y++)
        {
          uint8_t use_y = 8 - y;
          for (x = 0; x < 9; x++)
            {
              if (playfield[use_y][x] == EMPTY_TILE)
                {
                  if (use_y > 0 && (background[use_y - 1][x] & ~BG_MASK))
                    playfield[use_y][x] = EMPTY_TILE;
                  else if (use_y > 0)
                    {
                      if (playfield[use_y - 1][x] != EMPTY_TILE)
                        some_movement = 1;
                      playfield[use_y][x] = playfield[use_y - 1][x];
                      playfield[use_y - 1][x] = EMPTY_TILE;
                    }
                  else
                    {
                      playfield[0][x] = rng ();
                      some_movement = 1;
                    }

                  redraw_tile (x, use_y);

                  some_explosions = 1;
                }
            }
        }
    }
  while (some_explosions && some_movement);
}

static void
selected_state (uint8_t selected)
{
  if (selected)
    {
      for (uint8_t c = 0; c < 8; c++)
        setpalette (c + 8, 8);
    }
  else
    {
      for (uint8_t c = 0; c < 8; c++)
        setpalette (c + 8, 7);
    }
}

static void
make_special (uint8_t base, uint8_t *x)
{
  uint8_t candy = *x;
  candy &= 127;
  if (candy < 6)
    candy += base;
  *x = candy;
}

static char
special_candy (uint8_t *position, uint8_t h_score, uint8_t v_score)
{
  if (h_score >= 5 || v_score >= 5)
    {
      thescore += 20;
      *position = COLOURBOMB_TILE;
    }
  else if (h_score >= 3 && v_score >= 3)
    {
      thescore += 20;
      make_special (WRAP_TILES, position);
    }
  else if (h_score >= 4)
    {
      thescore += 10;
      make_special (H_TILES, position);
    }
  else if (v_score >= 4)
    {
      thescore += 10;
      make_special (V_TILES, position);
    }
  else
    return 0;

  return 1;
}

static uint8_t
permitted_swap (uint8_t oldx, uint8_t oldy, uint8_t newx, uint8_t newy)
{
  uint8_t lhs = playfield[oldy][oldx];
  uint8_t rhs = playfield[newy][newx];

  /* Swapping a colour with itself isn't a "move".  */
  if (lhs < FIRST_NONCOLOUR
      && rhs < FIRST_NONCOLOUR
      && (lhs % 6) == (rhs % 6))
    return 0;

  /* Disallow illegal moves.  */
  if ((rhs >= FIRST_NONCOLOUR
       || lhs >= FIRST_NONCOLOUR)
      && lhs != COLOURBOMB_TILE
      && rhs != COLOURBOMB_TILE
      && lhs != EMPTY_TILE
      && rhs != EMPTY_TILE)
    return 0;

  /* Can't swap with cages or swirls.  */
  if ((background[oldy][oldx] & ~BG_MASK)
      || (background[newy][newx] & ~BG_MASK))
    return 0;

  return 1;
}

static uint8_t
successful_move (uint8_t oldx, uint8_t oldy, uint8_t newx, uint8_t newy)
{
  uint8_t selected_tile;
  uint8_t h_score = 0, v_score = 0, success = 0;
  uint8_t lhs = playfield[oldy][oldx];
  uint8_t rhs = playfield[newy][newx];

  if (!permitted_swap (oldx, oldy, newx, newy))
    return 0;

  do_swap (oldx, oldy, newx, newy);

  success = stripes_match (oldx, oldy, newx, newy, 1);

  success |= colourbomb_match (&playfield[newy][newx],
                               &playfield[oldy][oldx], 1);

  selected_tile = playfield[newy][newx];
  h_score = horizontal_match (newx, newy, selected_tile, 1);
  v_score = vertical_match (newx, newy, selected_tile, 1);

  success |= h_score >= 3 || v_score >= 3;
  if (!special_candy (&playfield[newy][newx], h_score, v_score))
    thescore += 5;

  selected_tile = playfield[oldy][oldx];
  h_score = horizontal_match (oldx, oldy, selected_tile, 1);
  v_score = vertical_match (oldx, oldy, selected_tile, 1);

  success |= h_score >= 3 || v_score >= 3;
  if (!special_candy (&playfield[oldy][oldx], h_score, v_score))
    thescore += 5;

  if (success)
    return 1;

  /* Undo the move.  */
  do_swap (oldx, oldy, newx, newy);

  return 0;
}

static uint8_t
move_is_possible (uint8_t oldx, uint8_t oldy, uint8_t newx, uint8_t newy)
{
  uint8_t success = 0;
  
  if (!permitted_swap (oldx, oldy, newx, newy))
    return 0;
  
  if (stripes_match (oldx, oldy, newx, newy, 0))
    return 1;

  if (colourbomb_match (&playfield[newy][newx], &playfield[oldy][oldx], 0))
    return 1;
  
  do_swap (oldx, oldy, newx, newy);
  if (horizontal_match (oldx, oldy, playfield[oldy][oldx], 0) >= 3
      || vertical_match (oldx, oldy, playfield[oldy][oldx], 0) >= 3
      || horizontal_match (newx, newy, playfield[newy][newx], 0) >= 3
      || vertical_match (newx, newy, playfield[newy][newx], 0) >= 3)
    success = 1;
  do_swap (oldx, oldy, newx, newy);
  return success;
}

static void big_text (uint8_t *, char *, uint8_t, uint8_t);

static uint8_t
reshuffle (void)
{
  uint8_t i, j;
  uint8_t *cp = &playfield[0][0];

  selected_state (0);
  big_text (&screenbase[10*ROWLENGTH+CENTRE (9)], "Reshuffle", 0xff, 0xc0);

  for (i = 0; i < 81; i++)
    {
      if (cp[i] < FIRST_NONCOLOUR)
        {
          uint8_t range = 81 - (i + 1), replacement, tmp, any_to_swap = 0;
          
          for (j = i + 1; i < 81; j++)
            if (cp[j] >= FIRST_NONCOLOUR)
              {
                any_to_swap = 1;
                break;
              }
          
          if (any_to_swap)
            {
              do
                {
                  replacement = i + 1 + rand () % range;
                }
              while (cp[replacement] >= FIRST_NONCOLOUR);
              tmp = cp[i];
              cp[i] = cp[replacement];
              cp[replacement] = tmp;
            }
          redraw_tile (i % 9, i / 9);
        }
    }

  big_text (&screenbase[10*ROWLENGTH+CENTRE (9)], "Reshuffle", 0x3f, 0x0);
}

static uint8_t
reshuffle_needed (void)
{
  uint8_t x, y;

  for (x = 0; x < 8; x++)
    for (y = 0; y < 8; y++)
      {
        if (move_is_possible (x, y, x + 1, y))
          return 0;

        if (move_is_possible (x, y, x, y + 1))
          return 0;
      }

  return 1;
}

static uint8_t
retrigger (void)
{
  uint8_t x, y;
  uint8_t success = 0;
  
  for (y = 0; y < 9; y++)
    for (x = 0; x < 9; x++)
      {
        if (horizontal_match (x, y, playfield[y][x], 1) >= 3)
          success = 1;
        if (vertical_match (x, y, playfield[y][x], 1) >= 3)
          success = 1;
      }

  return success;
}

static void
do_explosions (void)
{
  show_explosions ();
  pause (20000);
  shuffle_explosions ();
  reset_playfield_marks ();
}

static void
write_number (uint8_t *at, unsigned long number, uint8_t digits)
{
  unsigned long maximum = 1;
  uint8_t i;
  
  for (i = 1; i < digits; i++)
    maximum *= 10;

  for (i = 0; i < digits; i++)
    {
      uint8_t *digit = tiles[DIGITS_TEXT];
      digit += ((number / maximum) % 10) * 16;
      memcpy (at, digit, 16);
      at += 16;
      maximum /= 10;
    }
}

static void
big_text (uint8_t *chartop, char *str, uint8_t andval, uint8_t orval)
{
  static uint8_t exploded[9] = { 1 };
  uint8_t x, y, c;

  while (*str)
    {
      char thechar = *str++;
      uint8_t *charscan = chartop;
      exploded[0] = thechar;
      osword (10, exploded);
      for (y = 0; y < 8; y++)
        {
          uint8_t row = exploded[y + 1];
          uint8_t *screenrow = charscan;
          for (x = 0; x < 8; x++)
            {
              if (row & 0x80)
                {
                  for (c = 0; c < 8; c++)
                    screenrow[c] = (screenrow[c] & andval) | orval;
                }
              screenrow += 8;
              row <<= 1;
            }
          charscan += ROWLENGTH;
        }
      chartop += 8 * 8;
    }
}

static void
refresh_status (void)
{
  write_number (&screenbase[ROWLENGTH * 27 + 14 * 8], movesleft, 3);
  write_number (&screenbase[ROWLENGTH * 27 + 32 * 8], jellies, 2);
  write_number (&screenbase[ROWLENGTH * 27 + 51 * 8], thescore, 9);
}

static void
count_jelly (void)
{
  uint8_t cnt = 0;
  uint8_t x, y;
  
  for (y = 0; y < 9; y++)
    for (x = 0; x < 9; x++)
      {
        uint8_t bg_tile = background[y][x] & BG_MASK;
        if (bg_tile == 1 || bg_tile == 2)
          cnt++;
      }

  jellies = cnt;
}

static char *magic_words[] =
  {
    "Sugar", "Smash",
    "Fruity", "Yumyums"
  };    

static void
write_exciting_logo (uint8_t wordno)
{
  char *word1 = magic_words[wordno];
  char *word2 = magic_words[wordno + 1];
  uint8_t len1 = strlen (word1), len2 = strlen (word2);
  selected_state (1);
  big_text (&screenbase[5 * ROWLENGTH + CENTRE (len1)], word1, 0xff, 0xc0);
  big_text (&screenbase[15 * ROWLENGTH + CENTRE (len2)], word2, 0xff, 0xc0);
  pause (40000);
  big_text (&screenbase[5 * ROWLENGTH + CENTRE (len1)], word1, 0x3f, 0x0);
  big_text (&screenbase[15 * ROWLENGTH + CENTRE (len2)], word2, 0x3f, 0x0);
}

//static unsigned rowmultab[32];

static void
clear (void)
{
  unsigned ctr, offset;
  for (offset = 0; offset < 8; offset++)
    for (ctr = 0; ctr < ROWLENGTH * 27; ctr += 8)
      screenbase[ctr+offset] &= 0xc0;
}

/* squidge:
    0, -15, 7, 15
    1, 2, 200, 15
*/

static void
sound (int channel, int amplitude, int pitch, int duration)
{
  static uint8_t params[8];
  params[0] = channel & 255;
  params[1] = (channel >> 8) & 255;
  params[2] = amplitude & 255;
  params[3] = (amplitude >> 8) & 255;
  params[4] = pitch & 255;
  params[5] = (pitch >> 8) & 255;
  params[6] = duration & 255;
  params[7] = (duration >> 8) & 255;
  osword (7, params);
}

static void
init_level (uint8_t levelno)
{
  char *levdata = tiles[LEVEL1_PTR + levelno - 1];
  movesleft = levdata[0];
  memcpy (background, &levdata[1], 9 * 9);
}

static uint8_t
play_level (uint8_t levelno)
{
  uint8_t x, y;
  uint8_t oldcx, oldcy, cursx = 0, cursy = 0;
  signed char row, rep;
  uint8_t selected = 0;
  uint8_t i;

  init_level (levelno);

  memset (&screenbase[ROWLENGTH*27], 0x30, ROWLENGTH);
  memcpy (&screenbase[ROWLENGTH*27 + 2 * 8], tiles[MOVES_TEXT], 11 * 8);
  memcpy (&screenbase[ROWLENGTH*27 + 23 * 8], tiles[JELLY_TEXT], 8 * 8);
  memcpy (&screenbase[ROWLENGTH*27 + 39 * 8], tiles[SCORE_TEXT], 10 * 8);

  //thescore = 0;

  count_jelly ();
  refresh_status ();

  selected_state (0);

  do
    {
      for (y = 0; y < 9; y++)
        for (x = 0; x < 9; x++)
          playfield[y][x] = 255;

      for (y = 0; y < 9; y++)
        for (x = 0; x < 9; x++)
          {
            uint8_t thistile;
            do
              {
                if (background[y][x] & SWIRL_MASK)
                  thistile = SWIRL_TILE;
                else if ((background[y][x] & BG_MASK) == 3)
                  thistile = EMPTY_TILE;
                else
                  thistile = rng ();
                playfield[y][x] = thistile;
              }
            while (horizontal_match (x, y, thistile, 0) >= 3
                   || vertical_match (x, y, thistile, 0) >= 3);
          }
    }
  while (reshuffle_needed ());

  reset_playfield_marks ();

  for (y = 0; y < 9; y++)
    for (x = 0; x < 9; x++)
      redraw_tile (x, y);

  // Box!
  //gfx_gcol (1, 8);
  box (cursx, cursy, 0xff, 0xc0);

  // Flush input buffer.
  osbyte (15, 1, 0);

  if (reshuffle_needed ())
    return 1;

  while (movesleft > 0 && jellies > 0)
    {
      uint8_t readchar;
      oldcx = cursx;
      oldcy = cursy;

      readchar = osrdch ();

      switch (readchar)
        {
        case 136: /* left */
          if (cursx > 0)
            cursx--;
          break;
        case 137: /* right */
          if (cursx < 8)
            cursx++;
          break;
        case 138: /* down */
          if (cursy < 8)
            cursy++;
          break;
        case 139: /* up */
          if (cursy > 0)
            cursy--;
          break;
#ifdef CHEATMODE
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case 'h':
        case 'v':
        case 'w':
          box (cursx, cursy, 0x3f, 0x00);
          if (readchar == 'h' || readchar == 'H')
            make_special (H_TILES, &playfield[y][x]);
          else if (readchar == 'v' || readchar == 'V')
            make_special (V_TILES, &playfield[y][x]);
          else if (readchar == 'w' || readchar == 'W')
            make_special (WRAP_TILES, &playfield[y][x]);
          else
            playfield[cursy][cursx] = readchar - '1';
          redraw_tile (cursx, cursy);
          box (cursx, cursy, 0xff, 0xc0);
          break;
        case 'r': case 'R':
          redraw_tile (cursx, cursy);
          break;
#endif
        case 13:
          selected = !selected;
          selected_state (selected);
          break;
        default:
          ;
        }

      if (oldcx != cursx || oldcy != cursy)
        {
          uint8_t selected_tile = playfield[oldcy][oldcx];

          if (selected
              && successful_move (oldcx, oldcy, cursx, cursy))
            {
              uint8_t retriggers = 0;
              uint8_t plus_sound = 66;
              show_swap (oldcx, oldcy, cursx, cursy);

              sound (0x12, 1, 50, 10);

              do_explosions ();

              while (1)
                {
                  while (retrigger ())
                    {
                      sound (0x12, 1, plus_sound, 10);
                      do_explosions ();
                      retriggers++;
                      plus_sound += 16;
                    }

                  if (!reshuffle_needed ())
                    break;

                  reshuffle ();
                }

              if (retriggers > 2)
                write_exciting_logo (0);

              /* OR with 8.  */
              //gfx_gcol (1, 8);
              box (cursx, cursy, 0xff, 0xc0);

              selected = 0;
              selected_state (selected);

              movesleft--;
              count_jelly ();
              refresh_status ();
            }
          else
            {
              if (selected)
                {
                  selected = 0;
                  selected_state (selected);
                }

              /* OR with 8.  */
              //gfx_gcol (1, 8);
              box (cursx, cursy, 0xff, 0xc0);

              /* AND with 7.  */
              //gfx_gcol (2, 7);
              box (oldcx, oldcy, 0x3f, 0x00);
            }
        }
    }

  box (cursx, cursy, 0x3f, 0x0);

  return movesleft > 0;
}

typedef struct
{
  uint8_t num;
  uint8_t steplen;
  int8_t pitch_delta_1;
  int8_t pitch_delta_2;
  int8_t pitch_delta_3;
  uint8_t steps_1;
  uint8_t steps_2;
  uint8_t steps_3;
  int8_t amp_delta_attack;
  int8_t amp_delta_decay;
  int8_t amp_delta_sustain;
  int8_t amp_delta_release;
  uint8_t attack_target;
  uint8_t decay_target;
} envelope;

static envelope env1 =
{
  1,
  5 | 128,
  12, 0, 0,
  9, 0, 0,
  0, -5, 0, -10,
  126, 80
};

static envelope env2 =
{
  2,
  3,
  40, -1, 0,
  10, 15, 0,
  0, 0, 0, 0,
  0, 0
};

static envelope env3 =
{
  3,
  3,
  5, -5, 0,
  2, 2, 0,
  0, -3, 0, -5,
  126, 0
};

static envelope *envs[] = { &env1, &env2, &env3 };

static void
config_envelopes (void)
{
  uint8_t i;
  for (i = 0; i < 3; i++)
    osword (8, envs[i]);
}

int main (void)
{
  int win;
  uint8_t current_level = 1;

  config_envelopes ();

#ifdef ROM
  //osfile_load ("tiles\r", (void*) 0xe00);
  //setmode (2);
#elif !defined(TILES_LINKED_IN)
  select_sram (4);
  osfile_load ("tiles\r", (void*) 0x5800);
  memcpy ((void*) 0x8000, (void*) 0x5800, 10240);
#endif

  //setmode (2);

  // Shrink the screen a bit (free up some RAM!).
  screen_start (screenbase);
  vdu_var (1, 72); // horizontal displayed
  vdu_var (2, 94); // horizontal sync position
  vdu_var (6, 28); // vertical displayed
  vdu_var (7, 32); // vertical sync position

  /* Unfortunately this doesn't really work -- not sure if you can make the
     OS graphics routines work with funny-sized displays.  */

  /*WRITE_BYTE (0x350, (unsigned)screenbase & 255);
  WRITE_BYTE (0x351, (unsigned)screenbase >> 8);
  WRITE_BYTE (0x352, ROWLENGTH & 255);
  WRITE_BYTE (0x353, ROWLENGTH >> 8);*/

  /*for (i = 0; i < 32; i++)
    {
      unsigned ent = i * ROWLENGTH;
      rowmultab[i] = ((ent >> 8) & 255) | ((ent & 255) << 8);
    }
  WRITE_BYTE (0xe0, ((unsigned) &rowmultab[0]) & 255);
  WRITE_BYTE (0xe1, ((unsigned) &rowmultab[0]) >> 8);*/


  /* Cursor keys produce character codes.  */
  osbyte (4, 1, 0);

  /* Flash speed.  */
  osbyte (9, 4, 0);
  osbyte (10, 4, 0);

  do
    {
      win = play_level (current_level);

      if (win)
        {
          write_exciting_logo (0);
          write_exciting_logo (2);
          selected_state (0);
          big_text (&screenbase[10*ROWLENGTH+CENTRE (4)], "WIN!", 0x0, 0xc0);

          /* You shall be stuck on the final level forever.  */
          if (tiles[LEVEL1_PTR + current_level] != 0)
            current_level++;
        }
      else
        big_text (&screenbase[10*ROWLENGTH+CENTRE (6)], "Failed", 0x0, 0xc0);

      clear ();
      osrdch ();
    }
  while (1);

  return 0;
}
