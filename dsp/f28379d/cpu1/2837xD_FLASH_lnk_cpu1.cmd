MEMORY
{
PAGE 0 :
   BEGIN            : origin = 0x080000, length = 0x000002
   RAMD0            : origin = 0x00B000, length = 0x000800
   RAMLS4           : origin = 0x00A000, length = 0x000800
   RESET            : origin = 0x3FFFC0, length = 0x000002

   FLASHA           : origin = 0x080002, length = 0x001FFE
   FLASHB           : origin = 0x082000, length = 0x002000
   FLASHC           : origin = 0x084000, length = 0x002000
   FLASHD           : origin = 0x086000, length = 0x002000
   FLASHE           : origin = 0x088000, length = 0x008000
   FLASHF           : origin = 0x090000, length = 0x008000
   FLASHG           : origin = 0x098000, length = 0x008000
   FLASHH           : origin = 0x0A0000, length = 0x008000
   FLASHI           : origin = 0x0A8000, length = 0x008000
   FLASHJ           : origin = 0x0B0000, length = 0x008000
   FLASHK           : origin = 0x0B8000, length = 0x002000
   FLASHL           : origin = 0x0BA000, length = 0x002000
   FLASHM           : origin = 0x0BC000, length = 0x002000
   FLASHN           : origin = 0x0BE000, length = 0x001FF0

PAGE 1 :
   BOOT_RSVD        : origin = 0x000002, length = 0x000121
   RAMM1            : origin = 0x000400, length = 0x0003F8

   CPUTOCLA1MSGRAM  : origin = 0x001480, length = 0x000040
   CLA1TOCPUMSGRAM  : origin = 0x001500, length = 0x000040

   RAMLS3           : origin = 0x009800, length = 0x000800
   RAMLS5           : origin = 0x00A800, length = 0x000800

   RAMGS0           : origin = 0x00C000, length = 0x001000
   RAMGS1           : origin = 0x00D000, length = 0x001000
   RAMGS2           : origin = 0x00E000, length = 0x001000
   RAMGS3           : origin = 0x00F000, length = 0x001000
   RAMGS4           : origin = 0x010000, length = 0x001000
   RAMGS5           : origin = 0x011000, length = 0x001000
   RAMGS6           : origin = 0x012000, length = 0x001000
   RAMGS7           : origin = 0x013000, length = 0x001000
   RAMGS8           : origin = 0x014000, length = 0x001000
   RAMGS9_DMA       : origin = 0x015000, length = 0x000200
   RAMGS9           : origin = 0x015200, length = 0x000E00
   RAMGS10          : origin = 0x016000, length = 0x001000
   RAMGS11          : origin = 0x017000, length = 0x001000
   RAMGS12          : origin = 0x018000, length = 0x001000
   RAMGS13          : origin = 0x019000, length = 0x001000
   RAMGS14          : origin = 0x01A000, length = 0x001000
   RAMGS15          : origin = 0x01B000, length = 0x000FF8

   CPU2TOCPU1RAM    : origin = 0x03F800, length = 0x000400
   CPU1TOCPU2RAM    : origin = 0x03FC00, length = 0x000400
}

SECTIONS
{
   codestart        : > BEGIN,  PAGE = 0, ALIGN(8)
   .cinit           : > FLASHB, PAGE = 0, ALIGN(8)
   .switch          : > FLASHB, PAGE = 0, ALIGN(8)
   .text            : >> FLASHC | FLASHE, PAGE = 0, ALIGN(8)
   .reset           : > RESET,  PAGE = 0, TYPE = DSECT
   .stack           : > RAMM1,  PAGE = 1

#if defined(__TI_EABI__)
   .init_array      : > FLASHB, PAGE = 0, ALIGN(8)
   .bss             : >> RAMGS0 | RAMGS1, PAGE = 1
   .bss:output      : > RAMLS3, PAGE = 1
   .bss:cio         : > RAMGS2, PAGE = 1
   .const           : > FLASHF, PAGE = 0, ALIGN(8)
   .data            : > RAMGS3, PAGE = 1
   .sysmem          : > RAMGS4, PAGE = 1
#else
   .pinit           : > FLASHB, PAGE = 0, ALIGN(8)
   .ebss            : >> RAMGS0 | RAMGS1, PAGE = 1
   .econst          : > FLASHF, PAGE = 0, ALIGN(8)
   .esysmem         : > RAMGS4, PAGE = 1
#endif

   .TI.ramfunc : {} LOAD = FLASHD,
                     RUN = RAMD0,
                     LOAD_START(RamfuncsLoadStart),
                     LOAD_SIZE(RamfuncsLoadSize),
                     LOAD_END(RamfuncsLoadEnd),
                     RUN_START(RamfuncsRunStart),
                     RUN_SIZE(RamfuncsRunSize),
                     RUN_END(RamfuncsRunEnd),
                     PAGE = 0, ALIGN(8)

   Cla1Prog         : LOAD = FLASHD,
                      RUN = RAMLS4,
                      LOAD_START(Cla1funcsLoadStart),
                      LOAD_END(Cla1funcsLoadEnd),
                      LOAD_SIZE(Cla1funcsLoadSize),
                      RUN_START(Cla1funcsRunStart),
                      PAGE = 0, ALIGN(8)

   CpuToCla1MsgRAM  : > CPUTOCLA1MSGRAM, PAGE = 1
   Cla1ToCpuMsgRAM  : > CLA1TOCPUMSGRAM, PAGE = 1
   Cla1DataRam0     : > RAMLS5, PAGE = 1
   .scratchpad      : > RAMLS5, PAGE = 1

   .dma_buffers     : > RAMGS9_DMA, PAGE = 1

   GROUP : > CPU1TOCPU2RAM, PAGE = 1
   {
      PUTBUFFER
      PUTWRITEIDX
      GETREADIDX
   }

   GROUP : > CPU2TOCPU1RAM, PAGE = 1
   {
      GETBUFFER : TYPE = DSECT
      GETWRITEIDX : TYPE = DSECT
      PUTREADIDX : TYPE = DSECT
   }
}
