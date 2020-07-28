#include "stepper.h"
#include "pwmcontroller.h"
#include "unistd.h"
#include <applibs/gpio.h>

// We are targeting the MT3620 Dev Kit
#include "eventloop_timer_utilities.h"

struct stepper
{
	int hStepper;
	int fdPins[4];
	int speed;
	int grayIndex;
	EventLoopTimer* timer;
};

// This is stepper motor specific.  The stepper motor we are using is the following:
// DCBA
// 0001
// 0011
// 0110
// 0100
// 1100
// 1000
// 1001
const int greyCodes[] = {1,3,2,6,4,12,8,9};

// This is how many steps are in a single 360 degree revolution.
const double stepsPerRev = 4096.0;

// This is the fastest the stepper motor can run and still correctly move through each step.
const double minSecPerRev = 3.5;

// This is how fast we will rotate the stepper motor when set to a speed of 1 out of 100.
const double maxSecPerRev = 60.0;

#define MAX_STEPPERS 2
struct stepper steppers[MAX_STEPPERS] = {0};
static int stepperId = 0;

// We setup a timer per stepper motor.
static void StepperTimerEventHandler0(EventLoopTimer* timer);
static void StepperTimerEventHandler1(EventLoopTimer* timer);
EventLoopTimerHandler eventHandlers[2] = { StepperTimerEventHandler0, StepperTimerEventHandler1 };

struct stepper *FindStepper(int hStepper)
{
	for (int i = 0; i < MAX_STEPPERS; i++)
	{
		if (steppers[i].hStepper == hStepper)
		{
			return &(steppers[i]);
		}
	}

	return NULL;
}

void TakeStep(struct stepper* stepperMotor)
{
	if (stepperMotor->hStepper == 0)
	{
		return;
	}

	if (stepperMotor->speed == 0)
	{
		for (int i = 0; i < 4; i++)
		{
			GPIO_SetValue(stepperMotor->fdPins[i], GPIO_Value_Low);
		}

		return;
	}

	int prevGrayCode = greyCodes[stepperMotor->grayIndex];
	stepperMotor->grayIndex += (stepperMotor->speed > 0)?1:-1;
	if (stepperMotor->grayIndex < 0)
	{
		stepperMotor->grayIndex = sizeof(greyCodes)/sizeof(greyCodes[0])-1;
	}
	else if (stepperMotor->grayIndex >= sizeof(greyCodes) / sizeof(greyCodes[0]))
	{
		stepperMotor->grayIndex = 0;
	}

	for (int i = 0; i < 4; i++)
	{
		if ((greyCodes[stepperMotor->grayIndex] ^ prevGrayCode) & (1 << i))
		{
			GPIO_SetValue(stepperMotor->fdPins[i], greyCodes[stepperMotor->grayIndex] & (1 << i) ? GPIO_Value_High : GPIO_Value_Low);
		}
	}
}

static void StepperTimerEventHandler0(EventLoopTimer *timer)
{
	if (ConsumeEventLoopTimerEvent(timer) != 0)
	{
		return;
	}

	TakeStep(&steppers[0]);
}

void StepperTimerEventHandler1(EventLoopTimer* timer)
{
	if (ConsumeEventLoopTimerEvent(timer) != 0)
	{
		return;
	}

	TakeStep(&steppers[1]);
}

int Stepper_Open(int pin1, int pin2, int pin3, int pin4, EventLoop* eventLoop)
{
	struct stepper s = {0};

	int index = 0;
	while (index < MAX_STEPPERS)
	{
		if (steppers[index].hStepper == 0)
		{
			break;
		}

		index++;
	}

	if (index == MAX_STEPPERS)
	{
		return MAX_STEPPERS_ALLOCATED;
	}

	s.fdPins[0] = GPIO_OpenAsOutput(pin1, GPIO_OutputMode_PushPull, GPIO_Value_Low);
	if (s.fdPins[0] == -1)
	{
		return FAILED_OPEN_GPIO;
	}

	s.fdPins[1] = GPIO_OpenAsOutput(pin2, GPIO_OutputMode_PushPull, GPIO_Value_Low);
	if (s.fdPins[1] == -1)
	{
		close(s.fdPins[0]);
		return FAILED_OPEN_GPIO;
	}

	s.fdPins[2] = GPIO_OpenAsOutput(pin3, GPIO_OutputMode_PushPull, GPIO_Value_Low);
	if (s.fdPins[2] == -1)
	{
		close(s.fdPins[0]);
		close(s.fdPins[1]);
		return FAILED_OPEN_GPIO;
	}

	s.fdPins[3] = GPIO_OpenAsOutput(pin4, GPIO_OutputMode_PushPull, GPIO_Value_Low);
	if (s.fdPins[3] == -1)
	{
		close(s.fdPins[0]);
		close(s.fdPins[1]);
		close(s.fdPins[2]);
		return FAILED_OPEN_GPIO;
	}

	struct timespec period = { .tv_sec = 1, .tv_nsec = 0 };
	s.timer = CreateEventLoopPeriodicTimer(eventLoop, eventHandlers[index], &period);
	if (s.timer == NULL)
	{
		close(s.fdPins[0]);
		close(s.fdPins[1]);
		close(s.fdPins[2]);
		close(s.fdPins[3]);
		return FAILED_INIT_TIMER;
	}

	s.speed = 0;
	s.hStepper = ++stepperId;

	steppers[index] = s;
	return s.hStepper;
}

int Stepper_Close(int hStepper)
{
	struct stepper* stepper = FindStepper(hStepper);
	if (stepper == NULL)
	{
		return -1;
	}

	DisposeEventLoopTimer(stepper->timer);
	stepper->timer = NULL;
	stepper->speed = 0;
	for (int i = 0; i < 4; i++)
	{
		GPIO_SetValue(stepper->fdPins[i], GPIO_Value_Low);
		close(stepper->fdPins[i]);
		stepper->fdPins[i] = 0;
	}
	stepper->hStepper = 0;

	return 0;
}

int Stepper_Move(int hStepper, int speed)
{
	struct stepper* stepper = FindStepper(hStepper);
	if (stepper == NULL)
	{
		return -1;
	}

	stepper->speed = speed;
	double secPerRev = (speed - 1) * (minSecPerRev - maxSecPerRev) / 99.0 + maxSecPerRev;
	const struct timespec stepPeriod = { .tv_sec = 0, .tv_nsec = (long int)(1000000000L * (secPerRev / stepsPerRev)) };
	SetEventLoopTimerPeriod(stepper->timer, &stepPeriod);

	return 0;
}
