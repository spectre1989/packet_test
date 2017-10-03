#include "common.h"




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
	uint32_t test_packet_size = 32;
	float packet_interval_s = 1.0f / (float)packets_per_s;
	const uint32_t num_packets = time_s * packets_per_s;
	char send_buffer[2048];
	char recv_buffer[2048];

	uint32_t packet_size = create_start_capture_packet(send_buffer, num_packets);
	while (true)
	{
		send_packet(sock, send_buffer, packet_size, &server_address);
		printf("sent Start_Capture\n");

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

	
	uint32_t packet_id = 0;
	packet_size = create_test_packet(send_buffer, packet_id, test_packet_size);
	while (true)
	{
		send_packet(sock, send_buffer, packet_size, &server_address);

		LARGE_INTEGER t;
		QueryPerformanceCounter(&t);

		++packet_id;
		if (packet_id == num_packets)
		{
			break;
		}

		create_test_packet(send_buffer, packet_id, test_packet_size);

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

	packet_size = create_end_capture_packet(send_buffer);
	send_packet(sock, send_buffer, packet_size, &server_address);
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
				uint32_t batch_id;
				uint32_t num_batches;
				read_results_packet(recv_buffer, bytes_received, &batch_id, &num_batches, results_packet_ids, results_packet_ts);
				
				if (!has_received_first_batch)
				{
					has_received_first_batch = true;

					batches_received = new bool[num_batches];
					for (uint32_t i = 0; i < num_batches; ++i)
					{
						batches_received[i] = false;
					}
				}

				printf("got results batch %d/%d\n", batch_id + 1, num_batches);

				if (!batches_received[batch_id])
				{
					batches_received[batch_id] = true;
					++num_batches_received;
				}

				packet_size = create_ack_results_packet(send_buffer, batch_id);
				send_packet(sock, send_buffer, packet_size, &server_address);

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
			send_packet(sock, send_buffer, packet_size, &server_address);
			QueryPerformanceCounter(&t);
		}
	}

	printf("done");
	while (true) {}


    return 0;
}

