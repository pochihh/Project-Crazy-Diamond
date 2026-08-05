
MEMORY
{
PAGE 0 :
   BEGIN            : origin = 0x080000, length = 0x000002     /* Boot-to-flash entry */
   FLASHA           : origin = 0x080002, length = 0x001FFE     /* Entry + vectors */
   FLASHB           : origin = 0x082000, length = 0x002000
   FLASHC           : origin = 0x084000, length = 0x002000     /* CLA LOAD */
   FLASHD           : origin = 0x086000, length = 0x002000     /* ramfunc LOAD */
   FLASHE           : origin = 0x088000, length = 0x008000     /* .text (large) */
   FLASHF           : origin = 0x090000, length = 0x008000     /* .text overflow */
   FLASHG           : origin = 0x098000, length = 0x008000
   FLASHH           : origin = 0x0A0000, length = 0x008000
   FLASHI           : origin = 0x0A8000, length = 0x008000
   FLASHJ           : origin = 0x0B0000, length = 0x008000
   FLASHK           : origin = 0x0B8000, length = 0x002000
   FLASHL           : origin = 0x0BA000, length = 0x002000
   FLASHM           : origin = 0x0BC000, length = 0x002000
   FLASHN           : origin = 0x0BE000, length = 0x001FF0
   RESET            : origin = 0x3FFFC0, length = 0x000002
   RAMD0            : origin = 0x00B000, length = 0x000800     /* ramfunc RUN */
   RAMLS3           : origin = 0x009800, length = 0x000800     /* CLA message RAM */
   RAMLS4           : origin = 0x00A000, length = 0x000800     /* CLA program RUN */

PAGE 1 :
   BOOT_RSVD       : origin = 0x000002, length = 0x000121
   RAMM0            : origin = 0x000123, length = 0x0002DD
   RAMM1            : origin = 0x000400, length = 0x0003F8

   /* CLA hardware message RAMs (fixed hardware addresses) */
   CPUTOCLA1MSGRAM : origin = 0x001480, length = 0x000040     /* CPU-to-CLA1 msg RAM (64 words) */
   CLA1TOCPUMSGRAM : origin = 0x001500, length = 0x000040     /* CLA1-to-CPU msg RAM (64 words) */

   RAMD1            : origin = 0x00B800, length = 0x000800
   RAMLS0           : origin = 0x008000, length = 0x000800
   RAMLS1           : origin = 0x008800, length = 0x000800
   RAMLS2           : origin = 0x009000, length = 0x000800
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
   RAMGS9_DMA       : origin = 0x015000, length = 0x000200     /* DMA buffers */
   RAMGS9           : origin = 0x015200, length = 0x000E00
   RAMGS13          : origin = 0x019000, length = 0x001000
   RAMGS14          : origin = 0x01A000, length = 0x001000
   RAMGS15          : origin = 0x01B000, length = 0x000FF8

   CPU2TOCPU1RAM    : origin = 0x03F800, length = 0x000400
   CPU1TOCPU2RAM    : origin = 0x03FC00, length = 0x000400
}


SECTIONS
{
   codestart        : > BEGIN,      PAGE = 0

   //
   // Code in FLASH
   //
   .text            : >> FLASHE | FLASHF | FLASHG, PAGE = 0
   .cinit           : > FLASHA,     PAGE = 0
   .switch          : > FLASHA,     PAGE = 0
   .reset           : > RESET,      PAGE = 0, TYPE = DSECT

   //
   // ramfuncs: loaded from FLASH, copied to RAM at startup, run from RAM.
   // The linker exports _RamfuncsLoadStart/_Size/_RunStart for the
   // memcpy in main() or Device_init().
   //
#ifdef __TI_COMPILER_VERSION__
   #if __TI_COMPILER_VERSION__ >= 15009000
    .TI.ramfunc : LOAD = FLASHD,
                  RUN  = RAMD0,
                  LOAD_START(RamfuncsLoadStart),
                  LOAD_SIZE(RamfuncsLoadSize),
                  RUN_START(RamfuncsRunStart),
                  PAGE = 0
   #endif
#endif

   //
   // Initialised data: .cinit copies from FLASH to these sections at startup
   //
   .stack           : > RAMM1,      PAGE = 1

#if defined(__TI_EABI__)
   .bss             : > RAMLS5,     PAGE = 1
   .bss:output      : > RAMLS2,     PAGE = 1
   .init_array      : > FLASHA,     PAGE = 0
   .const           : > FLASHB,     PAGE = 0
   .data            : > RAMLS5,     PAGE = 1
   .sysmem          : > RAMLS5,     PAGE = 1
#else
   .pinit           : > FLASHA,     PAGE = 0
   .ebss            : > RAMLS5,     PAGE = 1
   .econst          : > FLASHB,     PAGE = 0
   .esysmem         : > RAMLS5,     PAGE = 1
#endif

   //
   // DMA buffers: must remain in GS RAM accessible by DMA
   //
   .dma_buffers     : > RAMGS9_DMA, PAGE = 1

   //
   // CLA sections
   //
   /* CLA program: loads from FLASHC, runs from RAMLS4.                     */
   /* EABI mode: no underscore prefix on linker-exported symbols.           */
   Cla1Prog         : LOAD = FLASHC,
                      RUN  = RAMLS4,
                      LOAD_START(Cla1funcsLoadStart),
                      LOAD_SIZE(Cla1funcsLoadSize),
                      RUN_START(Cla1funcsRunStart),
                      PAGE = 0

   /* Hardware CLA message RAMs: fixed addresses, not configurable LS RAM.  */
   CpuToCla1MsgRAM  : > CPUTOCLA1MSGRAM, PAGE = 1
   Cla1ToCpuMsgRAM  : > CLA1TOCPUMSGRAM, PAGE = 1

   /* CLA data RAM (RAMLS5 configured CPU+CLA1 dual access in cla_setup.c)  */
   Cla1DataRam0     : > RAMLS5,    PAGE = 1

   //
   // IPC
   //
   GROUP : > CPU1TOCPU2RAM, PAGE = 1
   {
       PUTBUFFER
       PUTWRITEIDX
       GETREADIDX
   }

   GROUP : > CPU2TOCPU1RAM, PAGE = 1
   {
       GETBUFFER :    TYPE = DSECT
       GETWRITEIDX :  TYPE = DSECT
       PUTREADIDX :   TYPE = DSECT
   }
}

//===========================================================================
// NOTE: After linking with this file, call memcpy in main() before enabling
//       interrupts to copy .TI.ramfunc from FLASH to RAM:
//
//   extern uint16_t RamfuncsLoadStart, RamfuncsLoadSize, RamfuncsRunStart;  // EABI: no underscore
//   memcpy(&RamfuncsRunStart, &RamfuncsLoadStart, (size_t)&RamfuncsLoadSize);
//
//   (Or use Device_init() which handles this automatically on F28379D.)
//===========================================================================
