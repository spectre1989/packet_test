#include "common.h"

#include <stdio.h>


float time_since_s(LARGE_INTEGER t, LARGE_INTEGER freq)
{
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);

	return (float)(now.QuadPart - t.QuadPart) / (float)freq.QuadPart;
}

void send_packet(SOCKET sock, char* packet, int packet_size, sockaddr_in* address)
{
	int flags = 0;
	while (true)
	{
		int result = sendto(sock, packet, packet_size, flags, (sockaddr*)address, sizeof(*address));
		if (result == SOCKET_ERROR)
		{
			int error = WSAGetLastError();

			printf("sendto error %d\n", error);

			if (error == WSAEWOULDBLOCK ||
				error == WSAENOBUFS)
			{
				continue;
			}
		}

		break;
	}
}

int receive_packet(SOCKET sock, char* buffer, int buffer_size, sockaddr_in* address)
{
	int flags = 0;
	sockaddr_in from_address;
	int from_address_len = sizeof(from_address);
	int result = recvfrom(sock, buffer, buffer_size, flags, (sockaddr*)&from_address, &from_address_len);
	if (result == SOCKET_ERROR)
	{
		int error = WSAGetLastError();
		if (error != WSAEWOULDBLOCK &&
			error != WSAECONNRESET)
		{
			printf("recvfrom error %d\n", error);
		}
	}
	else
	{
		if (from_address.sin_addr.S_un.S_addr == address->sin_addr.S_un.S_addr &&
			from_address.sin_port == address->sin_port)
		{
			return result;
		}
		else
		{
			printf("received packet from unexpected machine\n");
		}
	}

	return 0;
}

// client msgs
uint32_t create_start_test_packet(char* buffer, uint32_t num_packets)
{
	buffer[0] = Client_Msg::Start_Test;
	memcpy(&buffer[1], &num_packets, 4);
	return 5;
}
void read_start_test_packet(char* buffer, uint32_t* num_packets)
{
	assert(buffer[0] == Client_Msg::Start_Test);
	memcpy(num_packets, &buffer[1], 4);
}

uint32_t create_test_packet(char* buffer, uint32_t id, uint32_t size)
{
	buffer[0] = Client_Msg::Test_Packet;
	memcpy(&buffer[1], &id, 4);

	for (uint32_t i = 5; i < size; ++i)
	{
		int r = rand();
		memcpy(&buffer[i], &r, 1);
	}

	return size;
}
void read_test_packet(char* buffer, uint32_t* id)
{
	assert(buffer[0] == Client_Msg::Test_Packet);
	memcpy(id, &buffer[1], 4);
}

uint32_t create_end_test_packet(char* buffer)
{
	buffer[0] = Client_Msg::End_Test;
	return 1;
}

// server msgs
uint32_t create_test_started_packet(char* buffer)
{
	buffer[0] = Server_Msg::Test_Started;
	return 1;
}

void create_test_packet_echo(char* original_packet)
{
	original_packet[0] = Server_Msg::Test_Packet_Echo;
}
void read_test_packet_echo(char* buffer, uint32_t* id)
{
	assert(buffer[0] == Server_Msg::Test_Packet_Echo);
	memcpy(id, &buffer[1], 4);
}

uint32_t create_results_packet(char* buffer, uint32_t num_dropped_packets, uint32_t num_duplicated_packets)
{
	buffer[0] = Server_Msg::Results;
	memcpy(&buffer[1], &num_dropped_packets, 4);
	memcpy(&buffer[5], &num_duplicated_packets, 4);
	return 9;
}
void read_results_packet(char* buffer, uint32_t* num_dropped_packets, uint32_t* num_duplicated_packets)
{
	assert(buffer[0] == Server_Msg::Results);

	memcpy(num_dropped_packets, &buffer[1], 4);
	memcpy(num_duplicated_packets, &buffer[5], 4);
}