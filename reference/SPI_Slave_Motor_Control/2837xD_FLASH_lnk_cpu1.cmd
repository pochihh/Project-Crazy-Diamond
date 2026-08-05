/* ========================================================================== */
/* F28379D CPU1 — FLASH linker, CLA enabled (LS5 program / LS4 data)         */
/* ========================================================================== */

MEMORY
{
/* ------------------------------ PAGE 0 (Program/Flash) -------------------- */
   PAGE 0 :
      /* Boot branch location (codestart) */
      BEGIN        : origin = 0x080000, length = 0x000002   /* codestart here */

      /* Flash sectors (no overlap with BEGIN) */
      FLASHA       : origin = 0x080002, length = 0x001FFE   /* remainder of A */
      FLASHB       : origin = 0x082000, length = 0x002000
      FLASHC       : origin = 0x084000, length = 0x002000
      FLASHD       : origin = 0x086000, length = 0x002000
      FLASHE       : origin = 0x088000, length = 0x008000
      FLASHF       : origin = 0x090000, length = 0x008000
      FLASHG       : origin = 0x098000, length = 0x008000
      FLASHH       : origin = 0x0A0000, length = 0x008000
      FLASHI       : origin = 0x0A8000, length = 0x008000
      FLASHJ       : origin = 0x0B0000, length = 0x008000
      FLASHK       : origin = 0x0B8000, length = 0x002000
      FLASHL       : origin = 0x0BA000, length = 0x002000
      FLASHM       : origin = 0x0BC000, length = 0x002000
      FLASHN       : origin = 0x0BE000, length = 0x001FF0   /* keep last 16B free */

      RESET        : origin = 0x3FFFC0, length = 0x000002

      /* CLA program will RUN from LS5 (program page) */
      RAMLS5       : origin = 0x00A800, length = 0x000800

      /* Small program RAM (ramfuncs runtime, etc.) */
      RAMM0        : origin = 0x000123, length = 0x0002DD
      RAMD0        : origin = 0x00B000, length = 0x000800

/* ------------------------------ PAGE 1 (Data/RAM) ------------------------- */
   PAGE 1 :
      BOOT_RSVD    : origin = 0x000002, length = 0x000121  /* BOOT ROM uses M0 */
      RAMM1        : origin = 0x000400, length = 0x0003F8
      RAMD1        : origin = 0x00B800, length = 0x000800

      /* CLA data RAM (data page) */
      RAMLS4       : origin = 0x00A000, length = 0x000800

      /* Global shared RAM for .bss/.data/.const */
      RAMGS0       : origin = 0x00C000, length = 0x001000
      RAMGS1       : origin = 0x00D000, length = 0x001000
      RAMGS2       : origin = 0x00E000, length = 0x001000
      RAMGS3       : origin = 0x00F000, length = 0x001000
      RAMGS4       : origin = 0x010000, length = 0x001000
      RAMGS5       : origin = 0x011000, length = 0x001000
      RAMGS6       : origin = 0x012000, length = 0x001000
      RAMGS7       : origin = 0x013000, length = 0x001000
      RAMGS8       : origin = 0x014000, length = 0x001000
      RAMGS9       : origin = 0x015000, length = 0x001000
      RAMGS10      : origin = 0x016000, length = 0x001000
      RAMGS11      : origin = 0x017000, length = 0x001000  /* 28379D-only */
      RAMGS12      : origin = 0x018000, length = 0x001000
      RAMGS13      : origin = 0x019000, length = 0x001000
      RAMGS14      : origin = 0x01A000, length = 0x001000
      RAMGS15      : origin = 0x01B000, length = 0x000FF8

      CPU2TOCPU1RAM: origin = 0x03F800, length = 0x000400
      CPU1TOCPU2RAM: origin = 0x03FC00, length = 0x000400

      CANA_MSG_RAM : origin = 0x049000, length = 0x000800
      CANB_MSG_RAM : origin = 0x04B000, length = 0x000800

      /* CLA1 message RAMs */
      CLA1TOCPUMSGRAM : origin = 0x001480, length = 0x000080
      CPUTOCLA1MSGRAM : origin = 0x001500, length = 0x000080
}

/* ========================================================================== */

SECTIONS
{
/* Boot & vectors */
   codestart        : > BEGIN,  PAGE = 0           /* F2837xD_CodeStartBranch */
   .reset           : > RESET,  PAGE = 0, TYPE = DSECT

/* ----------- Code & consts: LOAD in Flash, RUN in RAM (like RAM build) ----------- */
   .text            : >> FLASHE | FLASHF | FLASHG | FLASHH | FLASHI, PAGE = 0
   .cinit           : >  FLASHD, PAGE = 0
   .switch          : >  FLASHD, PAGE = 0
#if defined(__TI_EABI__)
   .const           : >  FLASHC, PAGE = 0
   .init_array      : >  FLASHC, PAGE = 0
#else
   .econst          : >  FLASHC, PAGE = 0
   .pinit           : >  FLASHC, PAGE = 0
#endif

/* ----------- Data / runtime in RAM (PAGE 1) - EXACTLY like RAM build ------------ */
   .stack           : >  RAMM1,  PAGE = 1
#if defined(__TI_EABI__)
   .bss             : >> RAMGS0 | RAMGS1 | RAMGS2 | RAMGS3 | RAMGS4 | RAMGS5, PAGE = 1
   .const           : >  RAMGS6, PAGE = 1
   .data            : >  RAMGS0, PAGE = 1
   .sysmem          : >  RAMGS1, PAGE = 1
   .init_array      : >  RAMM0, PAGE = 0
#else
   .ebss            : >> RAMGS0 | RAMGS1 | RAMGS2 | RAMGS3 | RAMGS4 | RAMGS5, PAGE = 1
   .econst          : >  RAMGS6, PAGE = 1
   .esysmem         : >  RAMGS1, PAGE = 1
   .pinit           : >  RAMM0, PAGE = 0
#endif


/* TI ramfuncs - Expanded for all critical functions */
#if defined(__TI_COMPILER_VERSION__) && (__TI_COMPILER_VERSION__ >= 15009000)
   .TI.ramfunc :
   {
      *(.TI.ramfunc*)
   }  LOAD = FLASHK, PAGE = 0
      RUN  = RAMM0 | RAMD0, PAGE = 0
      LOAD_START(RamfuncsLoadStart)
      LOAD_SIZE (RamfuncsLoadSize)
      RUN_START  (RamfuncsRunStart)
#else
   ramfuncs :
   {
      *(.TI.ramfunc*)
   }  LOAD = FLASHK, PAGE = 0
      RUN  = RAMM0 | RAMD0, PAGE = 0
      LOAD_START(RamfuncsLoadStart)
      LOAD_SIZE (RamfuncsLoadSize)
      RUN_START  (RamfuncsRunStart)
#endif

/* Optional peripheral example sections (keep or remove as needed) */
   Filter_RegsFile  : > RAMGS7, PAGE = 1
   ramgs0           : > RAMGS7, PAGE = 1
   ramgs1           : > RAMGS8, PAGE = 1

/* DMA buffers - dedicated space for SPI DMA */
   .dma_buffers     : > RAMGS9, PAGE = 1

/* IPC driver buffers (if you use IPC) */
   GROUP            : > CPU1TOCPU2RAM, PAGE = 1
   {
      PUTBUFFER
      PUTWRITEIDX
      GETREADIDX
   }
   GROUP            : > CPU2TOCPU1RAM, PAGE = 1
   {
      GETBUFFER      : TYPE = DSECT
      GETWRITEIDX    : TYPE = DSECT
      PUTREADIDX     : TYPE = DSECT
   }

/* --------------------------- CLA placement -------------------------------- */

/* CLA program: copy from Flash (e.g. FlashF) to LS5 and emit both
   underscored and non‑underscored copy symbols */
   Cla1Prog :
   {
      /* CLA code will be pulled in by the CPU vector reference */
   }   LOAD = FLASHF, PAGE = 0
      RUN  = RAMLS5, PAGE = 0
      LOAD_START(Cla1funcsLoadStart)    /* non‑underscore names used by board.c */
      LOAD_SIZE (Cla1funcsLoadSize)
      RUN_START (Cla1funcsRunStart)

   /* CLA constant table: copy from Flash (e.g. FlashE) to LS4 and emit copy
      symbols.  SysConfig’s board.c references Cla1Const*.  */
   Cla1Const :
   {
      *(.const_cla)      /* place your CLA constants here; adjust section name if needed */
   }   LOAD = FLASHE, PAGE = 0
      RUN  = RAMLS4,  PAGE = 1
      LOAD_START(Cla1ConstLoadStart)
      LOAD_SIZE (Cla1ConstLoadSize)
      RUN_START (Cla1ConstRunStart)

   /* CLA data and message sections remain unchanged */
   Cla1DataRam       : > RAMLS4,          PAGE = 1
   Cla1ToCpuMsgRAM   : > CLA1TOCPUMSGRAM, PAGE = 1
   CpuToCla1MsgRAM   : > CPUTOCLA1MSGRAM, PAGE = 1
}
