#ifndef stepper_stepper_h
#define stepper_stepper_h

#include <applibs/pwm.h>
#include "eventloop_timer_utilities.h"

#ifdef __cplusplus
extern "C"
{
#endif

	enum stepper_result_t
	{
		MAX_STEPPERS_ALLOCATED = -1,
		FAILED_OPEN_GPIO = -2,
		FAILED_INIT_TIMER = -3,
	};

	int Stepper_Open(int pin1, int pin2, int pin3, int pin4, EventLoop *eventLoop);
	int Stepper_Close(int hStepper);

	// Rotate stepper motor clockwise. speed=0..100
	// Rotate stepper motor counter-clockwise. speed=0..-100
	int Stepper_Move(int hStepper, int speed);
#ifdef __cplusplus
}
#endif

#endif
