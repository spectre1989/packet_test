#pragma once

#include <stdint.h>
#include <Windows.h>


#define assert(x) if(!(x)) { int* p = 0; *p = 0; }


enum Client_Msg : uint8_t
{
	Start_Capture,
	Test_Packet,
	End_Capture,
	Ack_Results
};

enum Server_Msg : uint8_t
{
	Capture_Started,
	Results
};


float time_since_s(LARGE_INTEGER* t, LARGE_INTEGER* freq);