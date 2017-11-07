#include "common.h"

#include <stdio.h>


float time_since_s(LARGE_INTEGER t, LARGE_INTEGER freq)
{
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);

	return (float)(now.QuadPart - t.QuadPart) / (float)freq.QuadPart;
}

uint32_t num_batches_needed_for_num_results(uint32_t num_results)
{
	return (num_results / c_num_results_per_batch) + ((num_results % c_num_results_per_batch) ? 1 : 0); // if results per batch doesn't exactly divide in to results count, then we need an extra batch
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

uint32_t create_ack_results_packet(char* buffer, uint32_t batch_id)
{
	buffer[0] = Client_Msg::Ack_Results;
	memcpy(&buffer[1], &batch_id, 4);
	return 5;
}
void read_ack_results_packet(char* buffer, uint32_t* batch_id)
{
	assert(buffer[0] == Client_Msg::Ack_Results);
	memcpy(batch_id, &buffer[1], 4);
}

// server msgs
uint32_t create_test_started_packet(char* buffer, LARGE_INTEGER clock_frequency)
{
	buffer[0] = Server_Msg::Test_Started;
	memcpy(&buffer[1], &clock_frequency.QuadPart, 8);
	return 9;
}
void read_test_started_packet(char* buffer, LARGE_INTEGER* server_clock_frequency)
{
	assert(buffer[0] == Server_Msg::Test_Started);
	memcpy(&server_clock_frequency->QuadPart, &buffer[1], 8);
}

uint32_t create_results_packet(char* buffer, uint32_t batch_id, uint32_t batch_start, uint32_t num_batches, uint32_t max_results_per_batch,
								uint32_t* results_ids, LARGE_INTEGER* results_ts, uint32_t results_count)
{
	uint32_t bytes_written = 0;

	buffer[0] = Server_Msg::Results;
	++bytes_written;

	memcpy(&buffer[bytes_written], &batch_id, 4);
	bytes_written += 4;

	memcpy(&buffer[bytes_written], &batch_start, 4);
	bytes_written += 4;

	memcpy(&buffer[bytes_written], &num_batches, 4);
	bytes_written += 4;
	
	assert(bytes_written == c_batch_header_size_in_bytes);
	for (uint32_t i = 0, results_i = batch_start; 
		i < max_results_per_batch && results_i < results_count; 
		++i, ++results_i)
	{
		memcpy(&buffer[bytes_written], &results_ids[results_i], 4);
		memcpy(&buffer[bytes_written + 4], &results_ts[results_i].QuadPart, 8);
		bytes_written += c_bytes_per_result;
	}

	return bytes_written;
}
void read_results_packet_header(char* buffer, uint32_t* batch_id, uint32_t* num_batches)
{
	assert(buffer[0] == Server_Msg::Results);

	memcpy(batch_id, &buffer[1], 4);
	memcpy(num_batches, &buffer[9], 4);
}
void read_results_packet_body(char* buffer, uint32_t packet_size, uint32_t* packet_ids, LARGE_INTEGER* packet_ts)
{
	assert(buffer[0] == Server_Msg::Results);

	uint32_t batch_start;
	memcpy(&batch_start, &buffer[5], 4);

	uint32_t batch_i = batch_start;
	uint32_t bytes_read = 13;
	while (bytes_read < packet_size)
	{
		memcpy(&packet_ids[batch_i], &buffer[bytes_read], 4);
		bytes_read += 4;
		memcpy(&packet_ts[batch_i].QuadPart, &buffer[bytes_read], 8);
		bytes_read += 8;
		++batch_i;
	}
}