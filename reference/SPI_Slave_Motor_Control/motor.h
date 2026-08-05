#ifndef MOTOR_H
#define MOTOR_H

#define MOTOR_A 0
#define MOTOR_B 1

void HBridge_init(void);
void motorControl(float controlInputA, float controlInputB);


#endif // MOTOR_H
