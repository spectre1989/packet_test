#include <stdint.h>
#include <stdio.h>
#include <WinSock2.h>
#include <Windows.h>


#define assert(x) if(!(x)) { int* p = 0; *p = 0; }


enum Client_Msg : uint8_t
{
	Start_Capture,
	Test_Packet,
	End_Capture
};

enum Server_Msg : uint8_t
{
	Capture_Started,
	Results
};


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

	UINT period_ms = 1;
	bool sleep_granularity_was_set = timeBeginPeriod(period_ms);

	LARGE_INTEGER clock_frequency;
	QueryPerformanceFrequency(&clock_frequency);

	sockaddr_in server_address;

	uint32_t time_s = 10;
	uint32_t packets_per_s = 5;
	uint32_t packet_size = 32;
	float packet_interval_s = 1.0f / (float)packets_per_s;
	uint32_t num_packets = time_s * packets_per_s;
	char send_buffer[2048];
	char recv_buffer[2048];

	send_buffer[0] = Client_Msg::Start_Capture;
	memcpy(&send_buffer[1], &num_packets, 4);
	while (true)
	{
		int flags = 0;
		sendto(sock, send_buffer, 5, flags, (sockaddr*)&server_address, sizeof(server_address));
		printf("sending Start_Capture\n");

		LARGE_INTEGER t;
		QueryPerformanceCounter(&t);

		bool got_reply = false;
		while (true)
		{
			sockaddr_in from_address;
			int from_address_len;
			result = recvfrom(sock, recv_buffer, sizeof(recv_buffer), flags, (sockaddr*)&from_address, &from_address_len);
			if (result == SOCKET_ERROR)
			{
				int error = WSAGetLastError();
				if (error != WSAEWOULDBLOCK)
				{
					printf("recvfrom error %d\n", error);
				}
			}
			else
			{
				if (from_address.sin_addr.S_un.S_addr == server_address.sin_addr.S_un.S_addr &&
					from_address.sin_port == server_address.sin_port &&
					recv_buffer[0] == Server_Msg::Capture_Started)
				{
					printf("got Capture_Started\n");
					got_reply = true;
					break;
				}
			}

			if (time_since_s(&t, &clock_frequency) > 5.0f)
			{
				break;
			}
		}

		if (got_reply)
		{
			break;
		}
	}

	// todo(jbr) randomly generate packet contents
	
	
	for (uint32_t i = 0; i < num_packets; ++i)
	{
		int flags = 0;
		sendto(sock, send_buffer, packet_size, flags, (sockaddr*)&server_address, sizeof(server_address));

		LARGE_INTEGER t;
		QueryPerformanceCounter(&t);

		if (result == SOCKET_ERROR)
		{
			printf("sendto error %d\n", WSAGetLastError());
		}

		while (true)
		{
			float delta_s = time_since_s(&t, &clock_frequency);
			
			if (sleep_granularity_was_set)
			{
				uint32_t time_since_ms = (uint32_t)(delta_s * 1000.0f); // intentional rounding down, don't want to oversleep
				if (time_since_ms > 0)
				{
					Sleep(time_since_ms);
				}
			}
			
			if (delta_s >= packet_interval_s)
			{
				break;
			}
		}
	}


    return 0;
}

