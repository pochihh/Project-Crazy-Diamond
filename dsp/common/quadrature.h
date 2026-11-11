#ifndef PCD_QUADRATURE_H
#define PCD_QUADRATURE_H
#include <stdint.h>
/* A is bit 0, B bit 1. Every legal edge contributes one count (x4). */
static inline int16_t pcd_quadrature_step(uint16_t before,uint16_t after) {
    static const int16_t delta[16]={0,1,-1,2,-1,0,2,1,1,2,0,-1,2,-1,1,0};
    return delta[((before&3U)<<2)|(after&3U)]; /* 2 means both bits changed. */
}
#endif
