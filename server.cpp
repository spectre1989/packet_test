#include <stdio.h>
#include <WinSock2.h>

#include "common.h"




int main(int argc, const char** argv)
{
	uint16_t port = c_port;

	for (int i = 1; i < argc; ++i)
	{
		if ((i + 1) < argc && strcmp(argv[i], "-p") == 0)
		{
			sscanf_s(argv[i + 1], "%hu", &port);
			i += 2;
		}
		else if(strcmp(argv[i], "?") == 0 || strcmp(argv[i], "help"))
		{
			printf("-p: port (default 9876)\ne.g. server.exe -p 7777\n");
			return 0;
		}
	}

	LARGE_INTEGER clock_frequency;
	QueryPerformanceFrequency(&clock_frequency);

	WSADATA wsa_data;
	int result = WSAStartup(0x202, &wsa_data);
	assert(result == 0);

	SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	u_long enabled = 1;
	result = ioctlsocket(sock, FIONBIO, &enabled);
	assert(result != SOCKET_ERROR);

	sockaddr_in local_address;
	local_address.sin_family = AF_INET;
	local_address.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
	local_address.sin_port = htons(port);
	bind(sock, (sockaddr*)&local_address, sizeof(local_address));

	char send_buffer[2048];
	char recv_buffer[2048];

	bool* packets_received = 0;
	uint32_t packets_received_capacity = 0;
	uint32_t num_dropped_packets = 0;
	uint32_t num_duplicated_packets = 0;

	sockaddr_in client_address;
	while (true)
	{
		// waiting
		printf("waiting for client\n");
		uint32_t num_packets;
		while (true)
		{
			int flags = 0;
			int client_address_len = sizeof(client_address);
			int num_bytes_received = recvfrom(sock, recv_buffer, sizeof(recv_buffer), flags, (sockaddr*)&client_address, &client_address_len);
			if (num_bytes_received != SOCKET_ERROR)
			{
				bool got_start_test = false;

				switch (recv_buffer[0])
				{
				case Client_Msg::Start_Test:
					printf("got Start_Test\n");
					read_start_test_packet(recv_buffer, &num_packets);

					if (packets_received_capacity < num_packets)
					{
						printf("resizing packets_received array from %d to %d\n", packets_received_capacity, num_packets);
						delete[] packets_received;
						packets_received_capacity = num_packets;
						packets_received = new bool[packets_received_capacity];
					}

					for (uint32_t i = 0; i < num_packets; ++i)
					{
						packets_received[i] = false;
					}
					num_duplicated_packets = 0;
					
					got_start_test = true;
					break;

				case Client_Msg::End_Test:
					// this means probably the results packet from the last test was dropped
					printf("got End_Test\n");
					uint32_t packet_size = create_results_packet(send_buffer, num_dropped_packets, num_duplicated_packets);
					send_packet(sock, send_buffer, packet_size, &client_address);
					printf("sent Results\n");

					break;
				}

				if (got_start_test)
				{
					break;
				}
			}
			else
			{
				int error = WSAGetLastError();
				if (error != WSAEWOULDBLOCK)
				{
					printf("recvfrom error %d\n", error);
				}
			}
		}

		// doing test
		uint32_t packet_size = create_test_started_packet(send_buffer);
		send_packet(sock, send_buffer, packet_size, &client_address);
		printf("sent Test_Started\n");
		LARGE_INTEGER time_test_started_was_sent;
		QueryPerformanceCounter(&time_test_started_was_sent);
		bool has_received_first_test_packet = false;
		bool end_test_received = false;
		LARGE_INTEGER timeout_timer = time_test_started_was_sent;
		bool timed_out = false;
		while (true)
		{
			int num_bytes_received = receive_packet(sock, recv_buffer, sizeof(recv_buffer), &client_address);
			if (num_bytes_received)
			{
				switch (recv_buffer[0])
				{
				case Client_Msg::Test_Packet:
					LARGE_INTEGER now;
					QueryPerformanceCounter(&now);
					uint32_t id;
					read_test_packet(recv_buffer, &id);

					create_test_packet_echo(recv_buffer);
					send_packet(sock, recv_buffer, num_bytes_received, &client_address);

					if (!has_received_first_test_packet)
					{
						has_received_first_test_packet = true;
					}

					if (packets_received[id])
					{
						++num_duplicated_packets;
					}
					else
					{
						packets_received[id] = true;
					}

					timeout_timer = now;
					
					break;

				case Client_Msg::End_Test:
					printf("got End_Test\n");
					end_test_received = true;
					break;

				// Ignore Start_Test, they'll just be 
				// out-of-order packets we don't care about by this point
				}
			}

			if (end_test_received)
			{
				break;
			}

			if (time_since_s(timeout_timer, clock_frequency) > 30.0f)
			{
				timed_out = true;
				printf("timed out\n");
				break;
			}

			if (!has_received_first_test_packet)
			{
				if (time_since_s(time_test_started_was_sent, clock_frequency) > 5.0f)
				{
					send_packet(sock, send_buffer, packet_size, &client_address);
					printf("sent Test_Started\n");
					QueryPerformanceCounter(&time_test_started_was_sent);
				}
			}
		}

		if (timed_out)
		{
			continue;
		}

		// sending results
		uint32_t num_dropped_packets = 0;
		for (uint32_t i = 0; i < num_packets; ++i)
		{
			if (!packets_received[i])
			{
				++num_dropped_packets;
			}
		}
		packet_size = create_results_packet(send_buffer, num_dropped_packets, num_duplicated_packets);
		send_packet(sock, send_buffer, packet_size, &client_address);
		printf("sent Results\n");
	}
	
    return 0;
}

