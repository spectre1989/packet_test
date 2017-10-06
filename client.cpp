#include <stdio.h>
#include <time.h>

#include "common.h"




int main()
{
	srand((unsigned int)time(0));

	WSADATA wsa_data;
	int result = WSAStartup(0x202, &wsa_data);
	assert(result == 0);

	SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	u_long enabled = 1;
	result = ioctlsocket(sock, FIONBIO, &enabled);
	assert(result != SOCKET_ERROR);

	UINT period_ms = 1;
	bool sleep_granularity_was_set = timeBeginPeriod(period_ms) == TIMERR_NOERROR;

	LARGE_INTEGER clock_frequency;
	QueryPerformanceFrequency(&clock_frequency);

	sockaddr_in server_address;
	server_address.sin_family = AF_INET;
	server_address.sin_addr.S_un.S_un_b.s_b1 = 127;
	server_address.sin_addr.S_un.S_un_b.s_b2 = 0;
	server_address.sin_addr.S_un.S_un_b.s_b3 = 0;
	server_address.sin_addr.S_un.S_un_b.s_b4 = 1;
	server_address.sin_port = htons(c_port);

	struct Test_Config
	{
		uint32_t duration_s;
		uint32_t packets_per_s;
		uint32_t packet_size;
	};

	char send_buffer[2048];
	char recv_buffer[2048];
	LARGE_INTEGER server_clock_frequency;

	uint32_t* results_packet_ids = 0;
	LARGE_INTEGER* results_packet_ts = 0;
	uint32_t results_capacity = 0;
	uint32_t results_count = 0;
	bool* results_batches_received = 0;
	uint32_t results_batches_received_capacity = 0;

	Test_Config test_configs[4] = {{10, 10, 32},{10, 20, 32},{10, 30, 32},{10, 40, 32}};
	uint32_t num_test_configs = 4;
	FILE* out_file = 0;

	for (uint32_t i = 0; i < num_test_configs; ++i)
	{
		uint32_t num_packets = test_configs[i].packets_per_s * test_configs[i].duration_s;
		uint32_t num_batches_needed = num_batches_needed_for_num_results(num_packets);

		if (num_batches_needed > results_batches_received_capacity)
		{
			results_batches_received_capacity = num_batches_needed;
			results_capacity = results_batches_received_capacity * c_num_results_per_batch;
		}
	}
	results_packet_ids = new uint32_t[results_capacity];
	results_packet_ts = new LARGE_INTEGER[results_capacity];
	results_batches_received = new bool[results_batches_received_capacity];
	
	Test_Config* test_config = &test_configs[0];
	Test_Config* test_config_end = &test_configs[num_test_configs];
	while (test_config != test_config_end)
	{
		float packet_interval_s = 1.0f / (float)test_config->packets_per_s;
		const uint32_t num_packets = test_config->duration_s * test_config->packets_per_s;

		// starting test
		uint32_t packet_size = create_start_test_packet(send_buffer, num_packets);
		while (true)
		{
			send_packet(sock, send_buffer, packet_size, &server_address);
			printf("sent Start_Test\n");

			LARGE_INTEGER t;
			QueryPerformanceCounter(&t);

			bool got_reply = false;
			while (true)
			{
				if (receive_packet(sock, recv_buffer, sizeof(recv_buffer), &server_address))
				{
					if (recv_buffer[0] == Server_Msg::Test_Started)
					{
						printf("got Test_Started\n");
						read_test_started_packet(recv_buffer, &server_clock_frequency);
						got_reply = true;
						break;
					}
					// can safely ignore results packets here, server will 
					// stop sending them when they get Start_Test
				}

				if (time_since_s(t, clock_frequency) > 5.0f)
				{
					break;
				}
			}

			if (got_reply)
			{
				break;
			}
		}

		
		// doing test
		uint32_t packet_id = 0;
		packet_size = create_test_packet(send_buffer, packet_id, test_config->packet_size);
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

			create_test_packet(send_buffer, packet_id, test_config->packet_size);

			while (true)
			{
				float time_since_last_packet_sent_s = time_since_s(t, clock_frequency);
				float time_until_next_packet_s = packet_interval_s - time_since_last_packet_sent_s;
				
				if (time_until_next_packet_s > 0.0f)
				{
					if (sleep_granularity_was_set)
					{
						uint32_t time_until_next_packet_ms = (uint32_t)(time_until_next_packet_s * 1000.0f); // intentional rounding down, don't want to oversleep
						if (time_until_next_packet_ms > 0)
						{
							Sleep(time_until_next_packet_ms);
						}
					}
				}
				else
				{
					break;
				}
			}
		}

		// get results
		bool has_received_first_batch = false;
		uint32_t num_batches = 0;
		uint32_t num_batches_received = 0;
		results_count = 0;

		packet_size = create_end_test_packet(send_buffer);
		send_packet(sock, send_buffer, packet_size, &server_address);
		printf("sent End_Test\n");
		LARGE_INTEGER time_end_test_sent;
		QueryPerformanceCounter(&time_end_test_sent);
		LARGE_INTEGER timeout_timer = time_end_test_sent;
		bool timed_out = false;
		while (true)
		{
			int num_bytes_received = receive_packet(sock, recv_buffer, sizeof(recv_buffer), &server_address);
			if (num_bytes_received)
			{
				if (recv_buffer[0] == Server_Msg::Results)
				{
					uint32_t batch_id;
					read_results_packet_header(recv_buffer, &batch_id, &num_batches);
					
					if (!has_received_first_batch)
					{
						has_received_first_batch = true;

						// it's possible with packet duplication that this array won't be big enough,
						// very unlikely though
						if (results_batches_received_capacity < num_batches)
						{
							printf("resizing batches_received from %d to %d\n", results_batches_received_capacity, num_batches);

							delete[] results_batches_received;
							results_batches_received_capacity = num_batches;
							results_batches_received = new bool[results_batches_received_capacity];

							delete[] results_packet_ids;
							delete[] results_packet_ts;
							results_capacity = results_batches_received_capacity * c_num_results_per_batch;
							results_packet_ids = new uint32_t[results_capacity];
							results_packet_ts = new LARGE_INTEGER[results_capacity];
						}

						for (uint32_t i = 0; i < num_batches; ++i)
						{
							results_batches_received[i] = false;
						}
					}

					printf("got batch %d/%d\n", batch_id + 1, num_batches);

					if (!results_batches_received[batch_id])
					{
						results_batches_received[batch_id] = true;
						++num_batches_received;

						read_results_packet_body(recv_buffer, num_bytes_received, results_packet_ids, results_packet_ts);

						// if it's the last batch, calculate the total number of results
						if (batch_id == num_batches - 1)
						{
							results_count = ((num_batches - 1) * c_num_results_per_batch) + ((num_bytes_received - c_batch_header_size_in_bytes) / c_bytes_per_result);
						}
					}

					packet_size = create_ack_results_packet(send_buffer, batch_id);
					send_packet(sock, send_buffer, packet_size, &server_address);

					QueryPerformanceCounter(&timeout_timer);
				}
				// Test_Started packet can be ignored here
			}

			if (num_batches > 0 && num_batches_received == num_batches)
			{
				break;
			}

			if (time_since_s(timeout_timer, clock_frequency) > 30.0f)
			{
				timed_out = true;
				printf("timed out\n");
				break;
			}

			if (!has_received_first_batch && time_since_s(time_end_test_sent, clock_frequency) > 5.0f)
			{
				send_packet(sock, send_buffer, packet_size, &server_address);
				printf("sent End_Test\n");
				QueryPerformanceCounter(&time_end_test_sent);
			}
		}

		if (timed_out)
		{
			// try again
		}
		else
		{
			// write results
			if (!out_file)
			{
				errno_t err = fopen_s(&out_file, "results.json", "w");
				assert(!err);
				fprintf(out_file, "{\n\ttests: [\n");
			}
			else
			{
				fprintf(out_file, ",\n");
			}
			
			fprintf(out_file, "\t\t{\n\t\t\tduration_s: %d,\n\t\t\tpackets_per_s: %d,\n\t\t\tpacket_size: %d,\n\t\t\tpackets: [", test_config->duration_s, test_config->packets_per_s, test_config->packet_size);

			for (uint32_t i = 0; i < results_count; ++i)
			{
				if (i > 0)
				{
					fprintf(out_file, ",");
				}
				fprintf(out_file, "\n\t\t\t\t{id: %d, t: %f}", results_packet_ids[i], (double)results_packet_ts[i].QuadPart / (double)server_clock_frequency.QuadPart);
			}

			fprintf(out_file, "\n\t\t\t]\n\t\t}");

			// next config
			++test_config;
		}	
	}

	fprintf(out_file, "\n\t]\n}\n");


    return 0;
}

