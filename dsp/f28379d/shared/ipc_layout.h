#ifndef F28379D_IPC_LAYOUT_H
#define F28379D_IPC_LAYOUT_H
#include <stdint.h>
#define IPC_LAYOUT_VERSION 2U
#define IPC_CPU2_START 0xd1a2U
#define IPC_PUBLISH_US 200U
typedef struct {
    uint32_t sequence_begin;
    int32_t motor_count[3];
    uint32_t transition_error[3];
    uint32_t max_poll_cycles;
    uint32_t sequence_end;
} Cpu2EncoderSnapshot;
typedef struct {
    uint16_t abi_version,active_slot;
    Cpu2EncoderSnapshot slot[2];
} Cpu2ToCpu1Ipc;
/* CPU2 writes an inactive slot with an odd begin sequence, finishes with equal
 * even sequences, then publishes its index. CPU1 verifies before/after copy. */
#endif
