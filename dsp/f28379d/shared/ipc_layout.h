#ifndef F28379D_IPC_LAYOUT_H
#define F28379D_IPC_LAYOUT_H

#include <stdint.h>

#define IPC_LAYOUT_VERSION        1U
#define IPC_MOTOR_ENCODER_COUNT   3U
#define IPC_AUX_ENCODER_COUNT     4U
#define IPC_CPU2_ENCODER_COUNT    (IPC_MOTOR_ENCODER_COUNT + IPC_AUX_ENCODER_COUNT)
#define IPC_SNAPSHOT_SLOT_COUNT   2U

typedef struct {
    uint32_t sequence_begin;
    int32_t motor_count[IPC_MOTOR_ENCODER_COUNT];
    int32_t auxiliary_count[IPC_AUX_ENCODER_COUNT];
    uint32_t transition_error[IPC_CPU2_ENCODER_COUNT];
    uint32_t heartbeat;
    uint32_t sequence_end;
} Cpu2EncoderSnapshot;

typedef struct {
    uint16_t abi_version;
    uint16_t active_slot;
    Cpu2EncoderSnapshot slot[IPC_SNAPSHOT_SLOT_COUNT];
} Cpu2ToCpu1Ipc;

/*
 * CPU2 writes the inactive slot, finishes with equal nonzero even sequence
 * values, then switches active_slot. CPU1 accepts a copy only when the slot
 * index is unchanged and both sequence values still match and are even.
 * IPC flags are reserved for boot/configuration/acknowledgement, not edges.
 */

#endif
