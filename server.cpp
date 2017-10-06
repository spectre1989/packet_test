#include <stdio.h>
#include <WinSock2.h>

#include "common.h"




int main()
{
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
	local_address.sin_port = htons(c_port);
	bind(sock, (sockaddr*)&local_address, sizeof(local_address));

	LARGE_INTEGER clock_frequency;
	QueryPerformanceFrequency(&clock_frequency);

	char send_buffer[2048];
	char recv_buffer[2048];

	uint32_t* results_ids = 0;
	LARGE_INTEGER* results_ts = 0;
	uint32_t results_count = 0;
	uint32_t results_capacity = 0;
	bool* batch_acks = 0;
	uint32_t batch_acks_capacity = 0;

	while (true)
	{
		sockaddr_in client_address;

		// waiting
		printf("waiting for client\n");
		while (true)
		{
			int flags = 0;
			int client_address_len = sizeof(client_address);
			int result = recvfrom(sock, recv_buffer, sizeof(recv_buffer), flags, (sockaddr*)&client_address, &client_address_len);
			if (result != SOCKET_ERROR)
			{
				bool got_start_test = false;

				switch (recv_buffer[0])
				{
				case Client_Msg::Start_Test:
					printf("got Start_Test\n");
					uint32_t num_packets;
					read_start_test_packet(recv_buffer, &num_packets);

					// technically might not be enough if some packets get duplicated, 
					// but that's rare and if it happens we resize again
					if (results_capacity < num_packets)
					{
						printf("resizing results array from %d to %d\n", results_capacity, num_packets);
						delete[] results_ids;
						delete[] results_ts;
						results_capacity = num_packets;
						results_ids = new uint32_t[results_capacity];
						results_ts = new LARGE_INTEGER[results_capacity];
					}
					
					got_start_test = true;
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
		results_count = 0;
		uint32_t packet_size = create_test_started_packet(send_buffer, clock_frequency);
		send_packet(sock, send_buffer, packet_size, &client_address);
		printf("sent Test_Started\n");
		LARGE_INTEGER time_test_started_was_sent;
		QueryPerformanceCounter(&time_test_started_was_sent);
		LARGE_INTEGER time_first_test_packed_received;
		bool has_received_first_test_packed = false;
		LARGE_INTEGER time_last_test_packet_received;
		bool end_test_received = false;
		LARGE_INTEGER timeout_timer = time_test_started_was_sent;
		bool timed_out = false;
		while (true)
		{
			if (receive_packet(sock, recv_buffer, sizeof(recv_buffer), &client_address))
			{
				switch (recv_buffer[0])
				{
				case Client_Msg::Test_Packet:
					LARGE_INTEGER now;
					QueryPerformanceCounter(&now);
					uint32_t id;
					read_test_packet(recv_buffer, &id);

					if (results_count == results_capacity)
					{
						// if we've run out of space, that means there has been some duplicated packets,
						// that's rare so shouldn't need much extra capacity
						uint32_t new_results_capacity = results_capacity + 32;
						printf("resizing results array from %d to %d\n", results_capacity, new_results_capacity);
						
						uint32_t* new_results_ids = new uint32_t[new_results_capacity];
						LARGE_INTEGER* new_results_ts = new LARGE_INTEGER[new_results_capacity];
						for (uint32_t i = 0; i < results_capacity; ++i)
						{
							new_results_ids[i] = results_ids[i];
							new_results_ts[i] = results_ts[i];
						}
						delete[] results_ids;
						delete[] results_ts;
						results_ids = new_results_ids;
						results_ts = new_results_ts;
					}

					time_last_test_packet_received = now;

					if (!has_received_first_test_packed)
					{
						has_received_first_test_packed = true;
						time_first_test_packed_received = now;
					}

					results_ids[results_count] = id;
					results_ts[results_count].QuadPart = now.QuadPart - time_first_test_packed_received.QuadPart;
					++results_count;

					timeout_timer = now;
					
					break;

				case Client_Msg::End_Test:
					printf("got End_Test\n");
					end_test_received = true;
					QueryPerformanceCounter(&timeout_timer);
					break;

				// Ignore Start_Test or Ack_Packet, they'll just be 
				// out-of-order packets we don't care about by this point
				}
			}

			if (time_since_s(timeout_timer, clock_frequency) > 30.0f)
			{
				timed_out = true;
				printf("timed out\n");
				break;
			}

			if (results_count == 0)
			{

				if (time_since_s(time_test_started_was_sent, clock_frequency) > 5.0f)
				{
					send_packet(sock, send_buffer, packet_size, &client_address);
					printf("sent Test_Started\n");
					QueryPerformanceCounter(&time_test_started_was_sent);
				}
			}

			if (end_test_received && time_since_s(time_last_test_packet_received, clock_frequency) > 5.0f)
			{
				break;
			}
		}

		if (timed_out)
		{
			continue;
		}

		// sending results
		uint32_t num_batches = num_batches_needed_for_num_results(results_count);
		if (batch_acks_capacity < num_batches)
		{
			delete[] batch_acks;
			batch_acks_capacity = num_batches;
			batch_acks = new bool[batch_acks_capacity];
		}
		for (uint32_t i = 0; i < num_batches; ++i)
		{
			batch_acks[i] = false;
		}
		
		QueryPerformanceCounter(&timeout_timer);
		while (true)
		{
			bool all_acked = true;
			for (uint32_t i = 0; i < num_batches; ++i)
			{
				if (!batch_acks[i])
				{
					all_acked = false;

					uint32_t packet_size = create_results_packet(
						send_buffer,
						i, // batch id
						i * c_num_results_per_batch, // batch start
						num_batches,
						c_num_results_per_batch,
						results_ids,
						results_ts,
						results_count);
					send_packet(sock, send_buffer, packet_size, &client_address);
					printf("sent batch %d/%d\n", i + 1, num_batches);
				}
			}

			if (all_acked)
			{
				break;
			}

			if (time_since_s(timeout_timer, clock_frequency) > 30.0f)
			{
				printf("timed out\n");
				break;
			}

			Sleep(1000);
			
			while (receive_packet(sock, recv_buffer, sizeof(recv_buffer), &client_address))
			{
				switch (recv_buffer[0])
				{
				case Client_Msg::Ack_Results:
					uint32_t batch_id;
					read_ack_results_packet(recv_buffer, &batch_id);

					batch_acks[batch_id] = true;

					printf("batch %d/%d acked\n", batch_id + 1, num_batches);

					QueryPerformanceCounter(&timeout_timer);
					break;

				case Client_Msg::Start_Test:
					// this likely means an ack got lost, and now the client is starting a new test
					for (uint32_t i = 0; i < num_batches; ++i)
					{
						batch_acks[i] = true;
					}
					break;

					// Test_Packet and End_Test can be safely ignored
				}
			}
		}
	}
	
    return 0;
}

