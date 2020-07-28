#include "rotary_encoder.h"
#include "unistd.h"
#include <applibs/gpio.h>

int fdClock = -1;
int fdData = -1;
RotaryChangedHandler changedHandler;
EventLoopTimer *timer = NULL;

const struct timespec debounceEncoder = { .tv_sec = 0, .tv_nsec = 2 * 1000 * 1000 };
const struct timespec pollRotaryEncoder = { .tv_sec = 0, .tv_nsec = 1 * 1000 * 1000 };

void RotaryEncoder_Poll(EventLoopTimer* timer)
{
	GPIO_Value_Type v1;
	GPIO_Value_Type v2;
	GPIO_Value_Type v3;
	GPIO_Value_Type v4;

	if (ConsumeEventLoopTimerEvent(timer) != 0)
	{
		return;
	}

	GPIO_GetValue(fdClock, &v1);
	GPIO_GetValue(fdData, &v2);
	nanosleep(&debounceEncoder, NULL);
	GPIO_GetValue(fdClock, &v3);
	GPIO_GetValue(fdData, &v4);

	if (!v1 && !v3 && (v2 == v4))
	{
		struct timespec delay1ms = { .tv_sec = 0, .tv_nsec = 1 * 1000 * 1000 };
		while (!v1)
		{
			GPIO_GetValue(fdClock, &v1);
			nanosleep(&delay1ms, NULL);
		}

		changedHandler(v2 ? -1 : 1);
	}
}

int RotaryEncoder_Open(int pinCLK, int pinDT, EventLoop* eventLoop, RotaryChangedHandler handler)
{
	if (timer != NULL)
	{
		return -1;
	}

	fdClock = GPIO_OpenAsInput(pinCLK);
	if (fdClock == -1)
	{
		return - 1;
	}

	fdData = GPIO_OpenAsInput(pinDT);
	if (fdData == -1)
	{
		close(fdClock);
		fdClock = -1;
		return -1;
	}

	changedHandler = handler;

	timer = CreateEventLoopPeriodicTimer(eventLoop, RotaryEncoder_Poll, &pollRotaryEncoder);
	if (timer == NULL)
	{
		close(fdClock);
		close(fdData);
		fdClock = -1;
		fdData = -1;
		return -1;
	}

	return 1;
}

int RotaryEncoder_Close(int hEncoder)
{
	if (timer != NULL && hEncoder != -1)
	{
		DisposeEventLoopTimer(timer);
		timer = NULL;
		close(fdClock);
		close(fdData);
		fdClock = -1;
		fdData = -1;
	}

	return 0;
}