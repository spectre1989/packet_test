

static float time_since_s(LARGE_INTEGER* t, LARGE_INTEGER* freq)
{
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);

	LARGE_INTEGER time_since;
	time_since.QuadPart = now.QuadPart - t->QuadPart;

	return (float)time_since.QuadPart / (float)freq->QuadPart;
}


int main()
{
	WSADATA wsa_data;
	int result = WSAStartup(0x202, &wsa_data);
	assert(result == 0);

	SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	u_long enabled = 1;
	result = ioctlsocket(sock, FIONBIO, &enabled);
	assert(result != SOCKET_ERROR);

	LARGE_INTEGER clock_frequency;
	QueryPerformanceFrequency(&clock_frequency);
	
    return 0;
}

