#include <stdbool.h>
#include <string.h>
#include <stdio.h>

typedef unsigned short uint16_t;

static uint16_t lfsr = 0xace1u;

#define PLAIN_TILES 0
#define V_TILES 6
#define H_TILES 12
#define WRAP_TILES 18
#define SWIRL_TILE 24
#define CAGE_TILE 25
#define COLOURBOMB_TILE 26
#define EXPLOSION_TILE 27
#define BG_TILES 28

static char *const screenbase = (char *) 0x4300;

#define ROWLENGTH 576

#define WRITE_BYTE(A, V) (*(volatile char *) (A) = (V))

unsigned int rand (void)
{
  uint16_t bit;        /* Must be 16bit to allow bit<<15 later in the code */

  /* taps: 16 14 13 11; feedback polynomial: x^16 + x^14 + x^13 + x^11 + 1 */
  bit  = ((lfsr >> 0) ^ (lfsr >> 2) ^ (lfsr >> 3) ^ (lfsr >> 5) ) & 1;
  lfsr =  (lfsr >> 1) | (bit << 15);

  return lfsr;
}

static void
oswrch (char x)
{
  __asm__ __volatile__ ("jsr $ffee" : : "Aq" (x));
}

static char
osbyte (char a, char x, char y)
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
osword (unsigned char code, unsigned char *parameters)
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
setmode (char mode)
{
  oswrch (22);
  oswrch (mode);
}

static void
setpalette (unsigned char physical, unsigned char logical)
{
  static unsigned char params[5] = {0, 0, 0, 0, 0};
  params[0] = physical;
  params[1] = logical;
  osword (0xc, &params[0]);
}

static void
gfx_plot (char code, unsigned x, unsigned y)
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
vdu_var (char reg, char val)
{
  char n;
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

extern char *tiles[];

void
render_tile (char *addr, char tileno)
{
  char x, y, row;
  char *tileptr = tiles[tileno];
  char count = 0, type = 0;

  for (x = 0; x < 8; x++)
    {
      char *coladdr = addr;

      for (row = 0; row < 3; row++)
        {
          char *rowaddr = coladdr;

          for (y = 0; y < 8; y++)
            {
              if (count == 0)
                {
                  char byte = *tileptr++;
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
                  char screenbyte = *rowaddr;
                  screenbyte &= 0x55;
                  screenbyte |= *tileptr++;
                  *rowaddr = screenbyte;
                }
              else if (type == 3)
                {
                  char screenbyte = *rowaddr;
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

void render_solid_tile (char *addr, char tileno)
{
  char x, y, row;
  char *tileptr = tiles[tileno];

  for (x = 0; x < 8; x++)
    {
      char *coladdr = addr;

      for (row = 0; row < 3; row++)
        {
          memcpy (coladdr, tileptr, 8);
          tileptr += 8;
          coladdr += ROWLENGTH;
        }

      addr += 8;
    }
}

char rng (void)
{
  char rnum;
  
  do {
    rnum = rand() & 7;
  } while (rnum > 5);
  
  return rnum;
}

char playfield[9][9];

char background[9][9] =
  {
    { 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 1, 1, 0, 0, 0, 0, 0, 1, 1 },
    { 2, 2, 2, 2, 2, 2, 2, 2, 2 }
  };

static void
hline (char sx, char ex, char y, char andcol, char orcol)
{
  char *row = &screenbase[(y >> 3) * ROWLENGTH + (y & 7)];
  char x, len = ex - sx;

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
vline (char x, char sy, char ey, char andcol, char orcol)
{
  char *row = &screenbase[(sy >> 3) * ROWLENGTH + ((x & ~1) << 2)];
  char y;

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

static void box (char cursx, char cursy, char andcol, char orcol)
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
redraw_tile (char x, char y)
{
  char *tileat = &screenbase[y * ROWLENGTH * 3 + x * 8 * 8];
  render_solid_tile (tileat, BG_TILES + background[y][x]);
  render_tile (tileat, playfield[y][x] & 63);
}

static void
render_tile_xy (char x, char y, char tileno)
{
  char *tileat = &screenbase[y * ROWLENGTH * 3 + x * 8 * 8];
  render_tile (tileat, tileno);
}

static char
horizontal_match (char x, char y, char eq, char fix_matches)
{
  char c;
  char right = x, left = x, matching;
  
  for (c = x + 1; c < 9; c++)
    {
      if (playfield[y][c] == eq)
        right = c;
      else
        break;
    }

  for (c = x - 1; c != 255; c--)
    {
      if (playfield[y][c] == eq)
        left = c;
      else
        break;
    }

  matching = right - left + 1;

  if (fix_matches && matching >= 3)
    {
      for (x = left; x <= right; x++)
        playfield[y][x] |= 128;
    }

  return matching;
}

static char
vertical_match (char x, char y, char eq, char fix_matches)
{
  char c;
  char bottom = y, top = y, matching;
  
  for (c = y + 1; c < 9; c++)
    {
      if (playfield[c][x] == eq)
        bottom = c;
      else
        break;
    }

  for (c = y - 1; c != 255; c--)
    {
      if (playfield[c][x] == eq)
        top = c;
      else
        break;
    }

  matching = bottom - top + 1;

  if (fix_matches && matching >= 3)
    {
      for (y = top; y <= bottom; y++)
        playfield[y][x] |= 128;
    }

  return matching;
}

static void
reset_playfield_marks (void)
{
  char x, y;
  for (y = 0; y < 9; y++)
    for (x = 0; x < 9; x++)
      playfield[y][x] &= ~128;
}

static void
do_swap (char oldx, char oldy, char newx, char newy)
{
  char oldtile = playfield[oldy][oldx];
  playfield[oldy][oldx] = playfield[newy][newx];
  playfield[newy][newx] = oldtile;
}

static void
show_swap (char oldx, char oldy, char newx, char newy)
{
  redraw_tile (oldx, oldy);
  redraw_tile (newx, newy);
}

static void
show_explosions (void)
{
  char x, y;

  for (y = 0; y < 9; y++)
    for (x = 0; x < 9; x++)
      {
        if (playfield[y][x] & 128)
          render_tile_xy (x, y, EXPLOSION_TILE);
      }
}

void pause (int amt)
{
  long wait;
  for (wait = 0; wait < amt; wait++)
    __asm__ __volatile__ ("nop");
}

static void
shuffle_explosions (void)
{
  char x, y, y2;
  char some_explosions = 0;
  
  do
    {
      some_explosions = 0;
      for (y = 0; y < 9; y++)
        for (x = 0; x < 9; x++)
          {
            if (playfield[y][x] & 128)
              {
                if (y > 0)
                  {
                    playfield[y][x] = playfield[y - 1][x] & 127;
                    playfield[y - 1][x] |= 128;
                  }
                else
                  playfield[0][x] = rng ();

                redraw_tile (x, y);
                some_explosions = 1;
              }
          }
    }
  while (some_explosions);
}

static void
selected_state (char selected)
{
  if (selected)
    {
      for (char c = 0; c < 8; c++)
        setpalette (c + 8, 8);
    }
  else
    {
      for (char c = 0; c < 8; c++)
        setpalette (c + 8, c == 7 ? 0 : 7);
    }
}

static char
successful_move (char oldx, char oldy, char newx, char newy)
{
  char selected_tile;
  char success = 0;

  do_swap (oldx, oldy, newx, newy);

  selected_tile = playfield[newy][newx];
  if (horizontal_match (newx, newy, selected_tile, 1) >= 3)
    success = 1;

  if (vertical_match (newx, newy, selected_tile, 1) >= 3)
    success = 1;

  selected_tile = playfield[oldy][oldx];
  if (horizontal_match (oldx, oldy, selected_tile, 1) >= 3)
    success = 1;

  if (vertical_match (oldx, oldy, selected_tile, 1) >= 3)
    success = 1;

  if (success)
    return 1;

  /* Undo the move.  */
  do_swap (oldx, oldy, newx, newy);

  return 0;
}

static char
retrigger (void)
{
  char x, y;
  char success = 0;
  
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


//static unsigned rowmultab[32];

int main (void)
{
  unsigned char x, y;
  unsigned char oldcx, oldcy, cursx = 0, cursy = 0;
  signed char row, rep;
  unsigned char selected = 0;
  char i;

  //setmode (2);

  // Shrink the screen a bit (free up some RAM!).
  screen_start (screenbase);
  vdu_var (1, 72);
  vdu_var (6, 27);

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

  selected_state (0);

  /* Cursor keys produce character codes.  */
  osbyte (4, 1, 0);

  /* Flash speed.  */
  osbyte (9, 4, 0);
  osbyte (10, 4, 0);

  for (y = 0; y < 9; y++)
    for (x = 0; x < 9; x++)
      playfield[y][x] = 255;

  for (y = 0; y < 9; y++)
    for (x = 0; x < 9; x++)
      {
        char thistile;
        do
          {
            thistile = rng ();
            playfield[y][x] = thistile;
          }
        while (horizontal_match (x, y, thistile, 0) >= 3
               || vertical_match (x, y, thistile, 0) >= 3);
      }

  /*for (i = 0; i < 200; i++)
    hline (100 - i / 2, 100 + i / 3, i, 0xff, 0xc0);

  for (i = 0; i < 200; i++)
    hline (100 - i / 2, 100 + i / 3, i, 0x3f, 0x00);*/
    
  /*for (i = 0; i < 100; i++)
    vline (i, 50 - i / 2, 50 + i / 3, 0xff, 0xc0);*/

  reset_playfield_marks ();

  for (y = 0; y < 9; y++)
    for (x = 0; x < 9; x++)
      redraw_tile (x, y);

  // Box!
  //gfx_gcol (1, 8);
  box (cursx, cursy, 0xff, 0xc0);

  // Flush input buffer.
  osbyte (15, 1, 0);

  while (1)
    {
      oldcx = cursx;
      oldcy = cursy;

      switch (osrdch ())
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
        case 13:
          selected = !selected;
          selected_state (selected);
          break;
        default:
          ;
        }

        if (oldcx != cursx || oldcy != cursy)
          {
            char selected_tile = playfield[oldcy][oldcx];
            if (selected
                && successful_move (oldcx, oldcy, cursx, cursy))
              {
                show_swap (oldcx, oldcy, cursx, cursy);

                show_explosions ();
                pause (20000);
                shuffle_explosions ();
                reset_playfield_marks ();

                while (retrigger ())
                  {
                    show_explosions ();
                    pause (20000);
                    shuffle_explosions ();
                    reset_playfield_marks ();
                  }

                /* OR with 8.  */
                //gfx_gcol (1, 8);
                box (cursx, cursy, 0xff, 0xc0);

                selected = 0;
                selected_state (selected);
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

/*
  for (rep = 0; rep < 3; rep++)
    {
      for (row = 8 * 3 - 1; row > 0; row--)
        {
          char *rowat = &screenbase[row * 640];
          memcpy (rowat, rowat - 640, 640);
        }
      memset (screenbase, 0, 640);
    }*/

  return 0;
}
