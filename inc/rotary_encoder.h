#pragma once

#include "eventloop_timer_utilities.h"

#ifdef __cplusplus
extern "C"
{
#endif

	typedef void (*RotaryChangedHandler)(int increment);
	int RotaryEncoder_Open(int pinCLK, int pinDT, EventLoop* eventLoop, RotaryChangedHandler handler);
	int RotaryEncoder_Close(int hEncoder);

#ifdef __cplusplus
}
#endif
