#include "common.h"


float time_since_s(LARGE_INTEGER* t, LARGE_INTEGER* freq)
{
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);

	LARGE_INTEGER time_since;
	time_since.QuadPart = now.QuadPart - t->QuadPart;

	return (float)time_since.QuadPart / (float)freq->QuadPart;
}