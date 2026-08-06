#include <stdbool.h>

// CPU2 starts as a buildable, safe-idle image. Encoder ISR ownership and the
// 5 kHz IPC publisher are added here after the pin-level rate test passes.
void main(void)
{
    while (true) {
    }
}
