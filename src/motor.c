#include "motor.h"
#include "pwmcontroller.h"
#include "unistd.h"
#include <applibs/gpio.h>

struct motor
{
	int fdMotor;
	int fdPin1;
	int fdPin2;
	struct pwmController *pwmData;
	PWM_ChannelId pwmChannel;
	PwmState pwmState;
};

#define MAX_MOTORS 8
struct motor motors[MAX_MOTORS] = {0};
int motorId = 0;

int GetIndex(int fdMotor)
{
	for (int i = 0; i < MAX_MOTORS; i++)
	{
		if (motors[i].fdMotor == fdMotor)
		{
			return i;
		}
	}

	return -1;
}

int Motor_Open(int pin1, int pin2, PWM_ControllerId pwmController, PWM_ChannelId pwmChannel, unsigned int period_nsec)
{
	struct motor m = {0};

	int index = 0;
	while (index < MAX_MOTORS)
	{
		if (motors[index].fdMotor == 0)
		{
			break;
		}

		index++;
	}

	if (index == MAX_MOTORS)
	{
		return MAX_MOTORS_ALLOCATED;
	}

	m.fdPin1 = GPIO_OpenAsOutput(pin1, GPIO_OutputMode_PushPull, GPIO_Value_High);
	if (m.fdPin1 == -1)
	{
		return FAILED_OPEN_GPIO_PIN1;
	}

	m.fdPin2 = GPIO_OpenAsOutput(pin2, GPIO_OutputMode_PushPull, GPIO_Value_High);
	if (m.fdPin2 == -1)
	{
		close(m.fdPin1);
		return FAILED_OPEN_GPIO_PIN2;
	}

	m.pwmData = GetPwmController(pwmController);
	if (m.pwmData == NULL)
	{
		close(m.fdPin1);
		close(m.fdPin2);
		return FAILED_OPEN_PWM_CONTROLLER;
	}

	m.pwmChannel = pwmChannel;

	m.pwmState.dutyCycle_nsec = 0;
	m.pwmState.enabled = true;
	m.pwmState.polarity = PWM_Polarity_Normal; // High during duty cycle.
	m.pwmState.period_nsec = period_nsec;

	if (PWM_Apply(m.pwmData->fdPwm, m.pwmChannel, &m.pwmState) == -1)
	{
		close(m.fdPin1);
		close(m.fdPin2);
		return FAILED_APPLY_PWM;
	}

	m.fdMotor = ++motorId;
	motors[index] = m;

	return m.fdMotor;
}

int Motor_Close(int fdMotor)
{
	int i = GetIndex(fdMotor);
	if (i == -1)
	{
		return -1;
	}

	struct motor m = motors[i];

	m.pwmState.enabled = false;
	PWM_Apply(m.pwmData->fdPwm, m.pwmChannel, &m.pwmState);
	close(m.fdPin1);
	close(m.fdPin2);

	return 0;
}

int Motor_Move(int fdMotor, int speed)
{
	int i = GetIndex(fdMotor);
	if (i == -1)
	{
		return -1;
	}

	struct motor m = motors[i];

	if (speed > 0)
	{ // Clockwise
		if (GPIO_SetValue(m.fdPin1, GPIO_Value_High) == -1)
		{
			return -1;
		}

		if (GPIO_SetValue(m.fdPin2, GPIO_Value_Low) == -1)
		{
			return -1;
		}
	}
	else if (speed < 0)
	{ // Counter Clockwise
		if (GPIO_SetValue(m.fdPin1, GPIO_Value_Low) == -1)
		{
			return -1;
		}

		if (GPIO_SetValue(m.fdPin2, GPIO_Value_High) == -1)
		{
			return -1;
		}

		speed = -speed;
	}
	else
	{ // Break
		if (GPIO_SetValue(m.fdPin1, GPIO_Value_High) == -1)
		{
			return -1;
		}

		if (GPIO_SetValue(m.fdPin2, GPIO_Value_High) == -1)
		{
			return -1;
		}
	}

	m.pwmState.enabled = true;
	m.pwmState.dutyCycle_nsec = m.pwmState.period_nsec * (unsigned int)speed / 100;
	if (PWM_Apply(m.pwmData->fdPwm, m.pwmChannel, &m.pwmState) == -1)
	{
		return -1;
	}

	return 0;
}

int Motor_Coast(int fdMotor)
{
	int i = GetIndex(fdMotor);
	if (i == -1)
	{
		return -1;
	}

	struct motor m = motors[i];

	if (GPIO_SetValue(m.fdPin1, GPIO_Value_Low) == -1)
	{
		return -1;
	}

	if (GPIO_SetValue(m.fdPin2, GPIO_Value_Low) == -1)
	{
		return -1;
	}

	return 0;
}
