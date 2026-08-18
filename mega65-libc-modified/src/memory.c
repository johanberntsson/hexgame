#include <stdio.h>
#include <memory.h>

unsigned char dma_byte;
struct dmagic_dmalist dmalist;
struct dmagic_dmalist_transparent dmalist_transparent;

void do_dma(unsigned int dma)
{
  //  unsigned char i;
  mega65_io_enable();

  //  for(i=0;i<24;i++)
  //    printf("%02x ",PEEK(i+(unsigned int)&dmalist));
  //  printf("\n");
  //  while(1) continue;

  // Now run DMA job (to and from anywhere, and list is in low 1MB)
  POKE(0xd702U, 0);
  POKE(0xd704U, 0x00); // List is in $00xxxxx
  POKE(0xd701U, dma >> 8);
  POKE(0xd705U, dma & 0xff); // triggers enhanced DMA
}

unsigned char lpeek(long address)
{
  // Read the byte at <address> in 28-bit address space
  // XXX - Optimise out repeated setup etc
  // (separate DMA lists for peek, poke and copy should
  // save space, since most fields can stay initialised).

  dmalist.option_0b = 0x0b;
  dmalist.option_80 = 0x80;
  dmalist.source_mb = (address >> 20);
  dmalist.option_81 = 0x81;
  dmalist.dest_mb = 0x00; // dma_byte lives in 1st MB
  dmalist.option_85 = 0x85;
  dmalist.dest_skip = 1;
  dmalist.end_of_options = 0x00;
  dmalist.sub_cmd = 0x00;

  dmalist.command = 0x00; // copy
  dmalist.count = 1;
  dmalist.source_addr = address & 0xffff;
  dmalist.source_bank = (address >> 16) & 0x0f;
  dmalist.dest_addr = (unsigned int)&dma_byte;
  dmalist.dest_bank = 0;

  do_dma((unsigned int) &dmalist);

  return dma_byte;
}

unsigned char db1, db2, db3;

unsigned char lpeek_debounced(long address)
{
  db1 = 0;
  db2 = 1;
  while (db1 != db2 || db1 != db3)
  {
    db1 = lpeek(address);
    db2 = lpeek(address);
    db3 = lpeek(address);
  }
  return db1;
}

void lpoke(long address, unsigned char value)
{

  dmalist.option_0b = 0x0b;
  dmalist.option_80 = 0x80;
  dmalist.source_mb = 0x00; // dma_byte lives in 1st MB
  dmalist.option_81 = 0x81;
  dmalist.dest_mb = (address >> 20);
  dmalist.option_85 = 0x85;
  dmalist.dest_skip = 1;
  dmalist.end_of_options = 0x00;
  dmalist.sub_cmd = 0x00;

  dma_byte = value;
  dmalist.command = 0x00; // copy
  dmalist.count = 1;
  dmalist.source_addr = (unsigned int)&dma_byte;
  dmalist.source_bank = 0;
  dmalist.dest_addr = address & 0xffff;
  dmalist.dest_bank = (address >> 16) & 0x0f;

  do_dma((unsigned int) &dmalist);
  return;
}

void lcopy(long source_address, long destination_address,
           unsigned int count)
{
  dmalist.option_0b = 0x0b;
  dmalist.option_80 = 0x80;
  dmalist.source_mb = source_address >> 20;
  dmalist.option_81 = 0x81;
  dmalist.dest_mb = (destination_address >> 20);
  dmalist.option_85 = 0x85;
  dmalist.dest_skip = 1;
  dmalist.end_of_options = 0x00;
  dmalist.sub_cmd = 0x00;

  dmalist.command = 0x00; // copy
  dmalist.count = count;
  dmalist.sub_cmd = 0;
  dmalist.source_addr = source_address & 0xffff;
  dmalist.source_bank = (source_address >> 16) & 0x0f;
  // User should provide 28-bit address for IO
  // (otherwise we can't DMA to/from RAM under IO)
  //  if (source_address>=0xd000 && source_address<0xe000)
  //    dmalist.source_bank|=0x80;
  dmalist.dest_addr = destination_address & 0xffff;
  dmalist.dest_bank = (destination_address >> 16) & 0x0f;
  // User should provide 28-bit address for IO
  // (otherwise we can't DMA to/from RAM under IO)
  //  if (destination_address>=0xd000 && destination_address<0xe000)
  //    dmalist.dest_bank|=0x80;

  do_dma((unsigned int) &dmalist);
  return;
}

void lcopy_transparent(long source_address, long destination_address,
           unsigned int count, unsigned char transparent)
{
  dmalist_transparent.option_0b = 0x0b;
  dmalist_transparent.option_07 = 0x07;
  dmalist_transparent.option_80 = 0x80;
  dmalist_transparent.source_mb = source_address >> 20;
  dmalist_transparent.option_81 = 0x81;
  dmalist_transparent.dest_mb = (destination_address >> 20);
  dmalist_transparent.option_85 = 0x85;
  dmalist_transparent.dest_skip = 1;
  dmalist_transparent.option_86 = 0x86;
  dmalist_transparent.transparent = transparent;
  dmalist_transparent.end_of_options = 0x00;
  dmalist_transparent.sub_cmd = 0x00;

  dmalist_transparent.command = 0x00; // copy
  dmalist_transparent.count = count;
  dmalist_transparent.sub_cmd = 0;
  dmalist_transparent.source_addr = source_address & 0xffff;
  dmalist_transparent.source_bank = (source_address >> 16) & 0x0f;
  // User should provide 28-bit address for IO
  // (otherwise we can't DMA to/from RAM under IO)
  //  if (source_address>=0xd000 && source_address<0xe000)
  //    dmalist.source_bank|=0x80;
  dmalist_transparent.dest_addr = destination_address & 0xffff;
  dmalist_transparent.dest_bank = (destination_address >> 16) & 0x0f;
  // User should provide 28-bit address for IO
  // (otherwise we can't DMA to/from RAM under IO)
  //  if (destination_address>=0xd000 && destination_address<0xe000)
  //    dmalist.dest_bank|=0x80;

  do_dma((unsigned int) &dmalist_transparent);
  return;
}

void lfill(long destination_address, unsigned char value,
           unsigned int count)
{
  dmalist.option_0b = 0x0b;
  dmalist.option_80 = 0x80;
  dmalist.source_mb = 0x00;
  dmalist.option_81 = 0x81;
  dmalist.dest_mb = destination_address >> 20;
  dmalist.option_85 = 0x85;
  dmalist.dest_skip = 1;
  dmalist.end_of_options = 0x00;

  dmalist.command = 0x03; // fill
  dmalist.sub_cmd = 0;
  dmalist.count = count;
  dmalist.source_addr = value;
  dmalist.dest_addr = destination_address & 0xffff;
  dmalist.dest_bank = (destination_address >> 16) & 0x0f;
  // User should provide 28-bit address for IO
  // (otherwise we can't DMA to/from RAM under IO)
  //  if (destination_address>=0xd000 && destination_address<0xe000)
  //    dmalist.dest_bank|=0x80;

  do_dma((unsigned int) &dmalist);
  return;
}

void lfill_skip(long destination_address, unsigned char value,
                unsigned int count, unsigned char skip)
{
  dmalist.option_0b = 0x0b;
  dmalist.option_80 = 0x80;
  dmalist.source_mb = 0x00;
  dmalist.option_81 = 0x81;
  dmalist.dest_mb = destination_address >> 20;
  dmalist.option_85 = 0x85;
  dmalist.dest_skip = skip;
  dmalist.end_of_options = 0x00;

  dmalist.command = 0x03; // fill
  dmalist.sub_cmd = 0;
  dmalist.count = count;
  dmalist.source_addr = value;
  dmalist.dest_addr = destination_address & 0xffff;
  dmalist.dest_bank = (destination_address >> 16) & 0x0f;

  do_dma((unsigned int) &dmalist);
  return;
}

void mega65_io_enable(void)
{
  // Gate C65 IO enable
  POKE(0xd02fU, 0x47);
  POKE(0xd02fU, 0x53);
  // Force to full speed
  POKE(0, 65);
}
