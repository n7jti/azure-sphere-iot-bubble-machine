#ifndef pwmcontroller_pwmcontroller_h
#define pwmcontroller_pwmcontroller_h

#include <applibs/pwm.h>

#ifdef __cplusplus
extern "C"
{
#endif

	struct pwmController {
		PWM_ControllerId pwmController;
		int fdPwm;
	};

	struct pwmController *GetPwmController(PWM_ControllerId pwmController);
#ifdef __cplusplus
}
#endif

#endif
