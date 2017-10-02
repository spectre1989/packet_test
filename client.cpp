#include <stdio.h>
#include <WinSock2.h>

#include "common.h"




static void create_test_packet(char* buffer, uint32_t id, uint32_t size)
{
	buffer[0] = Client_Msg::Test_Packet;
	memcpy(&buffer[1], &id, 4);

	for (uint32_t i = 5; i < size; ++i)
	{
		int r = rand();
		memcpy(&buffer[i], &r, 1);
	}
}

static int receive_packet(SOCKET sock, char* buffer, int buffer_size, sockaddr_in* server_address)
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
		if (from_address.sin_addr.S_un.S_addr == server_address->sin_addr.S_un.S_addr &&
			from_address.sin_port == server_address->sin_port)
		{
			return result;
		}
		else
		{
			printf("received packet but not from server");
		}
	}

	return 0;
}


int main()
{
	srand(time(0));

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

	const uint32_t time_s = 10;
	const uint32_t packets_per_s = 5;
	uint32_t packet_size = 32;
	float packet_interval_s = 1.0f / (float)packets_per_s;
	const uint32_t num_packets = time_s * packets_per_s;
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
			if (receive_packet(recv_buffer, sizeof(recv_buffer), &server_address))
			{
				switch (recv_buffer[0])
				{
				case Server_Msg::Capture_Started:
					printf("got Capture_Started\n");
					got_reply = true;
					break;

					// todo(jbr) unexpected cases
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
	
	uint32_t packet_id = 0;
	create_test_packet(send_buffer, packet_id, packet_size);
	while (true)
	{
		int flags = 0;
		sendto(sock, send_buffer, packet_size, flags, (sockaddr*)&server_address, sizeof(server_address));

		LARGE_INTEGER t;
		QueryPerformanceCounter(&t);

		if (result == SOCKET_ERROR)
		{
			printf("sendto error %d\n", WSAGetLastError());
		}

		++packet_id;
		if (packet_id == num_packets)
		{
			break;
		}

		create_test_packet(send_buffer, packet_id, packet_size);

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

	LARGE_INTEGER results_frequency;
	uint32_t results_packet_ids[num_packets];
	LARGE_INTEGER results_packet_ts[num_packets];
	bool has_received_first_batch = false;
	bool* batches_received = 0;
	uint32_t num_batches = 0;
	uint32_t num_batches_received = 0;

	send_buffer[0] = Client_Msg::End_Capture;
	int flags = 0;
	sendto(sock, send_buffer, 1, flags, (sockaddr*)&server_address, sizeof(server_address));
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	while (true)
	{
		int bytes_received = receive_packet(recv_buffer, sizeof(recv_buffer), &server_address);
		if (bytes_received)
		{
			switch (recv_buffer[0])
			{
			case Server_Msg::Results:
				uint32_t batch_id = 0;
				uint32_t batch_start = 0;
				memcpy(&batch_id, &buffer[1], 4);
				memcpy(&batch_start, &buffer[5], 4);
				
				if (!has_received_first_batch)
				{
					has_received_first_batch = true;

					memcpy(&num_batches, &buffer[9], 4);

					batches_received = new bool[num_batches];
					for (uint32_t i = 0; i < num_batches; ++i)
					{
						batches_received[i] = false;
					}
				}

				printf("got results batch %d/%d\n", batch_id + 1, num_batches);

				if (!batches_received[batch_id])
				{
					uint32_t buffer_i = 13;
					for (uint32_t i = 0; buffer_i < bytes_received; ++i)
					{
						memcpy(&results_packet_ids[batch_start + i], &recv_buffer[buffer_i], 4);
						buffer_i += 4;
						memcpy(&results_packet_ts[batch_start + i].QuadPart, &recv_buffer[buffer_i], 8);
						buffer_i += 8;
					}

					batches_received[batch_id] = true;
					++num_batches_received;
				}

				send_buffer[0] = Client_Msg::Ack_Results;
				memcpy(&send_buffer[1], &batch_id, 4);
				int flags = 0;
				sendto(sock, send_buffer, 5, flags, (sockaddr*)&server_address, sizeof(server_address));

				break;

				// todo(jbr) unexpected cases
			}
		}

		if (num_batches_received == num_batches)
		{
			break;
		}

		if (!has_received_first_batch && time_since_s(&t, &clock_frequency) > 5.0f)
		{
			sendto(sock, send_buffer, 1, flags, (sockaddr*)&server_address, sizeof(server_address));
			QueryPerformanceCounter(&t);
		}
	}

	printf("done");
	while (true) {}


    return 0;
}

