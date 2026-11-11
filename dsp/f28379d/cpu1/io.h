#ifndef PCD_IO_H
#define PCD_IO_H
#include "../../common/protocol.h"
void Io_init(void);
void Io_tick(PcdTelemetry *frame, const PcdCommand *command);
#endif
