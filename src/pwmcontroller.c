#include "pwmcontroller.h"
#include "unistd.h"

#define MAX_CONTROLLERS 4
struct pwmController pwmControllers[MAX_CONTROLLERS] = {0};

struct pwmController *GetPwmController(PWM_ControllerId pwmController)
{
	int emptyIndex = -1;
	int index = 0;
	while (index < MAX_CONTROLLERS)
	{
		if (pwmControllers[index].fdPwm == 0)
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
