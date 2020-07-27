#ifndef motor_motor_h
#define motor_motor_h

#include <applibs/pwm.h>

#ifdef __cplusplus
extern "C"
{
#endif

	enum motor_result_t
	{
		MAX_MOTORS_ALLOCATED = -1,
		FAILED_OPEN_GPIO_PIN1 = -2,
		FAILED_OPEN_GPIO_PIN2 = -3,
		FAILED_OPEN_PWM_CONTROLLER = -4,
		FAILED_APPLY_PWM = -5,
	};

	// period_nsec is duration for one cycle (typically 100000 to 10000000)
	int Motor_Open(int pin1, int pin2, PWM_ControllerId pwmController, PWM_ChannelId pwmChannel, unsigned int period_nsec);
	int Motor_Close(int fdMotor);

	// Rotate motor clockwise. speed=0..100
	// Rotate motor counter-clockwise. speed=0..-100
	int Motor_Move(int fdMotor, int speed);

	// Allows the motor to coast to a stop
	int Motor_Coast(int fdMotor);

#ifdef __cplusplus
}
#endif

#endif
