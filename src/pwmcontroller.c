#include "pwmcontroller.h"
#include "unistd.h"

#define MAX_CONTROLLERS 4
struct pwmController pwmControllers[MAX_CONTROLLERS] = {0};

int initPwmController(void)
{
	for (int i = 0; i < MAX_CONTROLLERS; ++i)
	{
		pwmControllers[i].fdPwm = -1;
		pwmControllers[i].pwmController = 0;
	}

	return 0;
}

// Looks to find pwmController.  If we have it cached, we return it.  If we don't have it
// and there is an empty index, we cache it and return it.
struct pwmController *GetPwmController(PWM_ControllerId pwmController)
{
	int emptyIndex = -1;
	int index = 0;
	while (index < MAX_CONTROLLERS)
	{
		if (pwmControllers[index].fdPwm < 0)
		{
			if (emptyIndex == -1)
			{
				emptyIndex = index;
			}
		}
		else if (pwmControllers[index].pwmController == pwmController)
		{
			return &pwmControllers[index];
		}

		index++;
	}

	if (emptyIndex == -1)
	{
		// Max controllers are already in use.
		return NULL;
	}

	struct pwmController pwm = {0};
	pwm.pwmController = pwmController;
	pwm.fdPwm = PWM_Open(pwmController);
	if (pwm.fdPwm == -1)
	{
		// Failed to open PWM controller.
		return NULL;
	}

	pwmControllers[emptyIndex] = pwm;
	return &pwmControllers[emptyIndex];
}

void ClosePwmController(PWM_ControllerId pwmController)
{
	// go through the controller cache and close all the open file-handles for the controller
	for (int i = 0; i < MAX_CONTROLLERS; ++i)
	{
		if (pwmControllers[i].pwmController == pwmController)
		{
			pwmControllers[i].pwmController = 0;
			if (pwmControllers[i].fdPwm >= 0)
			{
				close(pwmControllers[i].fdPwm);
				pwmControllers[i].fdPwm = -1;
			}
		}
	}
}
