MEMORY
{
PAGE 0:
   BEGIN   : origin = 0x080000, length = 0x000002
   FLASHA  : origin = 0x080002, length = 0x001FFE
   FLASHB  : origin = 0x082000, length = 0x002000
   FLASHC  : origin = 0x084000, length = 0x002000
   RAMLS4  : origin = 0x00A000, length = 0x000800
   RESET   : origin = 0x3FFFC0, length = 0x000002
PAGE 1:
   BOOT_RSVD : origin = 0x000002, length = 0x000121
   RAMM1     : origin = 0x000400, length = 0x0003F8
   RAMLS5    : origin = 0x00A800, length = 0x000800
}
SECTIONS
{
   codestart   : > BEGIN, PAGE = 0
   .text       : > FLASHB, PAGE = 0, ALIGN(8)
   .cinit      : > FLASHA, PAGE = 0, ALIGN(8)
   .switch     : > FLASHA, PAGE = 0, ALIGN(8)
   .init_array : > FLASHA, PAGE = 0, ALIGN(8)
   .const      : > FLASHA, PAGE = 0, ALIGN(8)
   .reset      : > RESET, PAGE = 0, TYPE = DSECT
   .stack      : > RAMM1, PAGE = 1
   .bss        : > RAMLS5, PAGE = 1
   .bss:output : > RAMLS5, PAGE = 1
   .data       : > RAMLS5, PAGE = 1
   .sysmem     : > RAMLS5, PAGE = 1
   .TI.ramfunc : {} LOAD = FLASHC, RUN = RAMLS4,
       LOAD_START(RamfuncsLoadStart), LOAD_SIZE(RamfuncsLoadSize),
       RUN_START(RamfuncsRunStart), PAGE = 0, ALIGN(8)
}
