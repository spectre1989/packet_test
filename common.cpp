#include "common.h"


float time_since_s(LARGE_INTEGER* t, LARGE_INTEGER* freq)
{
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);

	LARGE_INTEGER time_since;
	time_since.QuadPart = now.QuadPart - t->QuadPart;

	return (float)time_since.QuadPart / (float)freq->QuadPart;
}

void send_packet(SOCKET sock, char* packet, int packet_size, sockaddr_in* address)
{
	int flags = 0;
	int result = sendto(sock, packet, packet_size, flags, (sockaddr*)address, sizeof(*address));
	if (result == SOCKET_ERROR)
	{
		printf("sendto error %d\n", WSAGetLastError());
	}
}

int receive_packet(SOCKET sock, char* buffer, int buffer_size, sockaddr_in* address)
{
	int flags = 0;
	sockaddr_in from_address;
	int from_address_len;
	int result = recvfrom(sock, buffer, buffer_size, flags, (sockaddr*)&from_address, &from_address_len);
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
uint32_t create_start_capture_packet(char* buffer, uint32_t num_packets)
{
	buffer[0] = Client_Msg::Start_Capture;
	memcpy(&buffer[1], &num_packets, 4);
	return 5;
}
void read_start_capture_packet(char* buffer, uint32_t* num_packets)
{
	assert(buffer[0] == Client_Msg::Start_Capture);
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

uint32_t create_end_capture_packet(char* buffer)
{
	buffer[0] = Client_Msg::End_Capture;
	return 1;
}

uint32_t create_ack_results_packet(char* buffer, uint32_t batch_id)
{
	buffer[0] = Client_Msg::Ack_Results;
	memcpy(&buffer[1], &batch_id, 4);
	return 5;
}
void read_ack_results_packet(char* buffer, uint32_t* batch_id)
{
	assert(buffer[0] == Client_Msg::Ack_Result);
	memcpy(batch_id, &buffer[1], 4);
}

// server msgs
uint32_t create_capture_started_packet(char* buffer)
{
	buffer[0] = Server_Msg::Capture_Started;
	return 1;
}

uint32_t create_results_packet(char* buffer, uint32_t batch_id, uint32_t batch_start, uint32_t num_batches, 
								uint32_t* packet_ids, LARGE_INTEGER* packet_ts, uint32_t packet_count)
{
	buffer[0] = Server_Msg::Results;
	memcpy(&buffer[1], batch_id, 4);
	memcpy(&buffer[5], batch_start, 4);
	memcpy(&buffer[9], num_batches, 4);
	
	uint32_t bytes_written = 13;
	for (uint32_t i = 0; i < packet_count; ++i)
	{
		memcpy(&buffer[bytes_written], &packet_ids[batch_start + i], 4);
		bytes_written += 4;
		memcpy(&buffer[bytes_written], &packet_ts[batch_start + i].QuadPart, 8);
		bytes_written += 8;
	}

	return bytes_written;
}
void read_results_packet(char* buffer, uint32_t packet_size, uint32_t* batch_id, uint32_t* num_batches, 
								uint32_t* packet_ids, LARGE_INTEGER* packet_ts)
{
	assert(buffer[0] == Server_Msg::Results);

	memcpy(batch_id, &buffer[1], 4);
	uint32_t batch_start;
	memcpy(&batch_start, &buffer[5], 4);
	memcpy(num_batches, &buffer[9], 4);

	uint32_t batch_i = batch_start;
	uint32_t bytes_read = 13;
	while (bytes_read < packet_size)
	{
		memcpy(&packet_ids[batch_i], &buffer[bytes_read], 4);
		bytes_read += 4;
		memcpy(&packet_ts[batch_i].QuadPart, &buffer[bytes_read], 8);
		bytes_read += 8;
	}
}