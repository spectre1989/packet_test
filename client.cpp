#include <stdio.h>
#include <time.h>

#include "common.h"


static bool get_arg(int argc, const char** argv, int* i, const char* match)
{
	if (strcmp(argv[*i], match) == 0)
	{
		++(*i);
		if (*i < argc)
		{
			return true;
		}
	}

	return false;
}

int main(int argc, const char** argv)
{
	struct Test_Config
	{
		uint32_t duration_s;
		uint32_t packets_per_s;
		uint32_t packet_size;
	};

	const char* server_ip = "127.0.0.1";
	uint16_t port = c_port;
	const char* out_file_name = "results.json";
	Test_Config* test_configs = 0;
	uint32_t num_tests = 0;

	const char* c_help_text = "type client.exe ? or client.exe help for instructions\n";
	for (int i = 1; i < argc; ++i)
	{
		if (get_arg(argc, argv, &i, "-s"))
		{
			server_ip = argv[i];
		}
		else if (get_arg(argc, argv, &i, "-p"))
		{
			sscanf_s(argv[i], "%hu", &port);
		}
		else if (get_arg(argc, argv, &i, "-o"))
		{
			out_file_name = argv[i];
		}
		else if (get_arg(argc, argv, &i, "-t"))
		{
			++num_tests;
		}
		else if (strcmp(argv[i], "?") == 0 || strcmp(argv[i], "help"))
		{
			printf("args:\n-s: server ip (default 127.0.0.1)\n-p: server port (default %hu)\n-o: output file name (default results.json)\n-t: test (duration(seconds),packets per second,packet size in bytes)\nexample: client.exe -s 1.2.3.4 -p 7777 -o out.json -t (30,30,256) -t (30,15,512) -t (30,1,1024)\n", c_port);
			return 0;
		}
		else
		{
			printf("unexpected arg %s\n", argv[i]);
			printf(c_help_text);
			return 0;
		}
	}

	if (num_tests == 0)
	{
		printf("no tests\n");
		printf(c_help_text);
		return 0;
	}

	LARGE_INTEGER server_clock_frequency;
	uint32_t* results_packet_ids = 0;
	LARGE_INTEGER* results_packet_ts = 0;
	uint32_t results_capacity = 0;
	uint32_t results_count = 0;
	bool* results_batches_received = 0;
	uint32_t results_batches_received_capacity = 0;

	test_configs = new Test_Config[num_tests];
	Test_Config* test_config_iter = &test_configs[0];
	for (int i = 1; i < argc; ++i)
	{
		if (get_arg(argc, argv, &i, "-t"))
		{
			sscanf_s(argv[i], "(%u,%u,%u)", &test_config_iter->duration_s, &test_config_iter->packets_per_s, &test_config_iter->packet_size);

			uint32_t num_packets = test_config_iter->packets_per_s * test_config_iter->duration_s;
			uint32_t num_batches_needed = num_batches_needed_for_num_results(num_packets);

			if (num_batches_needed > results_batches_received_capacity)
			{
				results_batches_received_capacity = num_batches_needed;
				results_capacity = results_batches_received_capacity * c_num_results_per_batch;
			}

			test_config_iter++;
		}
	}

	results_packet_ids = new uint32_t[results_capacity];
	results_packet_ts = new LARGE_INTEGER[results_capacity];
	results_batches_received = new bool[results_batches_received_capacity];

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
	server_address.sin_addr.S_un.S_addr = inet_addr(server_ip);
	server_address.sin_port = htons(port);

	char send_buffer[2048];
	char recv_buffer[2048];

	FILE* out_file = 0;
	
	Test_Config* test_config = &test_configs[0];
	Test_Config* test_config_end = &test_configs[num_tests];
	while (test_config != test_config_end)
	{
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
		LARGE_INTEGER start_time_counts;
		QueryPerformanceCounter(&start_time_counts);
		double time_s = 0.0;
		double packet_interval_s = 1.0 / (double)test_config->packets_per_s;
		while (true)
		{
			send_packet(sock, send_buffer, packet_size, &server_address);

			++packet_id;
			if (packet_id == num_packets)
			{
				break;
			}

			create_test_packet(send_buffer, packet_id, test_config->packet_size);

			// wait until next packet
			time_s += packet_interval_s;
			LONGLONG target_counts_since_start = (LONGLONG)(clock_frequency.QuadPart * time_s);
			while (true)
			{
				LARGE_INTEGER now;
				QueryPerformanceCounter(&now);

				LONGLONG counts_since_start = now.QuadPart - start_time_counts.QuadPart;
				
				if (counts_since_start < target_counts_since_start)
				{
					if (sleep_granularity_was_set)
					{
						LONGLONG counts_until_next_packet = target_counts_since_start - counts_since_start;
						double time_until_next_packet_s = (double)counts_until_next_packet / (double)clock_frequency.QuadPart;
						uint32_t time_until_next_packet_ms = (uint32_t)(time_until_next_packet_s * 1000.0);
						if (time_until_next_packet_ms > 5) // with a quantum of 1ms the sleep can be 2 or 3ms over
						{
							Sleep(time_until_next_packet_ms - 5);
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
				errno_t err = fopen_s(&out_file, out_file_name, "w");
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
	fflush(out_file);


    return 0;
}

