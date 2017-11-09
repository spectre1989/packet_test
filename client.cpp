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
	const char* server_ip = "127.0.0.1";
	uint16_t port = c_port;
	const char* out_file_name = "results.json";
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

	struct Test_Config
	{
		uint32_t duration_s;
		uint32_t packets_per_s;
		uint32_t packet_size;
	};
	Test_Config* test_configs = new Test_Config[num_tests];

	uint32_t num_packets_in_largest_test = 0;
	Test_Config* test_config_iter = &test_configs[0];
	for (int i = 1; i < argc; ++i)
	{
		if (get_arg(argc, argv, &i, "-t"))
		{
			sscanf_s(argv[i], "(%u,%u,%u)", &test_config_iter->duration_s, &test_config_iter->packets_per_s, &test_config_iter->packet_size);
			
			if (test_config_iter->packet_size > c_max_packet_size)
			{
				test_config_iter->packet_size = c_max_packet_size;
			}

			uint32_t num_packets = test_config_iter->packets_per_s * test_config_iter->duration_s;

			if (num_packets > num_packets_in_largest_test)
			{
				num_packets_in_largest_test = num_packets;
			}

			test_config_iter++;
		}
	}

	LARGE_INTEGER* packet_sent_ts = new LARGE_INTEGER[num_packets_in_largest_test];
	LARGE_INTEGER* packet_received_ts = new LARGE_INTEGER[num_packets_in_largest_test];

	srand((unsigned int)time(0));

	WSADATA wsa_data;
	int result = WSAStartup(0x202, &wsa_data);
	assert(result == 0);

	SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	u_long enabled = 1;
	result = ioctlsocket(sock, FIONBIO, &enabled);
	assert(result != SOCKET_ERROR);

	LARGE_INTEGER clock_frequency;
	QueryPerformanceFrequency(&clock_frequency);

	sockaddr_in server_address;
	server_address.sin_family = AF_INET;
	server_address.sin_addr.S_un.S_addr = inet_addr(server_ip);
	server_address.sin_port = htons(port);

	char send_buffer[c_max_packet_size];
	char recv_buffer[c_max_packet_size];

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
		
		
		LARGE_INTEGER start_time_counts;
		QueryPerformanceCounter(&start_time_counts);
		for (uint32_t packet_id = 0; packet_id < num_packets; ++packet_id)
		{
			packet_size = create_test_packet(send_buffer, packet_id, test_config->packet_size);

			send_packet(sock, send_buffer, packet_size, &server_address);

			QueryPerformanceCounter(&packet_sent_ts[packet_id]);
			packet_received_ts[packet_id].QuadPart = 0;

			// wait until next packet
			double wait_until_seconds = (double)(packet_id + 1) / (double)test_config->packets_per_s;
			LONGLONG wait_until_counts = (LONGLONG)(clock_frequency.QuadPart * wait_until_seconds);
			while (true)
			{
				if (receive_packet(sock, recv_buffer, sizeof(recv_buffer), &server_address))
				{
					if (recv_buffer[0] == Server_Msg::Test_Packet_Echo)
					{
						uint32_t packet_id;
						read_test_packet_echo(recv_buffer, &packet_id);

						if (!packet_received_ts[packet_id].QuadPart)
						{
							QueryPerformanceCounter(&packet_received_ts[packet_id]);
						}
					}
				}

				LARGE_INTEGER now;
				QueryPerformanceCounter(&now);

				LONGLONG counts_since_start = now.QuadPart - start_time_counts.QuadPart;
				
				if (counts_since_start >= wait_until_counts)
				{
					break;
				}
			}
		}

		// Wait for a bit for any echoes to come back
		LARGE_INTEGER t;
		QueryPerformanceCounter(&t);
		while (true)
		{
			if (receive_packet(sock, recv_buffer, sizeof(recv_buffer), &server_address))
			{
				if (recv_buffer[0] == Server_Msg::Test_Packet_Echo)
				{
					uint32_t packet_id;
					read_test_packet_echo(recv_buffer, &packet_id);

					if (!packet_received_ts[packet_id].QuadPart)
					{
						QueryPerformanceCounter(&packet_received_ts[packet_id]);
					}
				}
			}

			if (time_since_s(t, clock_frequency) > 5.0f)
			{
				break;
			}
		}

		// get results
		packet_size = create_end_test_packet(send_buffer);
		send_packet(sock, send_buffer, packet_size, &server_address);
		printf("sent End_Test\n");
		LARGE_INTEGER time_end_test_sent;
		QueryPerformanceCounter(&time_end_test_sent);
		LARGE_INTEGER timeout_timer = time_end_test_sent;
		bool timed_out = false;
		uint32_t results_num_dropped_packets = 0;
		uint32_t results_num_duplicated_packets = 0;
		while (true)
		{
			int num_bytes_received = receive_packet(sock, recv_buffer, sizeof(recv_buffer), &server_address);
			if (num_bytes_received)
			{
				if (recv_buffer[0] == Server_Msg::Results)
				{
					read_results_packet(recv_buffer, &results_num_dropped_packets, &results_num_duplicated_packets);
					printf("got Results\n");
					break;
				}
				// Test_Started packet can be ignored here
			}

			if (time_since_s(timeout_timer, clock_frequency) > 30.0f)
			{
				timed_out = true;
				printf("timed out\n");
				break;
			}

			if (time_since_s(time_end_test_sent, clock_frequency) > 5.0f)
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
			
			fprintf(out_file, "\t\t{\n\t\t\tduration_s: %d,\n\t\t\tpackets_per_s: %d,\n\t\t\tpacket_size: %d,\n\t\t\tnum_packets_dropped: %d,\n\t\t\tnum_packets_duplicated: %d,\n\t\t\tpackets: [", 
				test_config->duration_s, test_config->packets_per_s, test_config->packet_size, results_num_dropped_packets, results_num_duplicated_packets);

			for (uint32_t i = 0; i < num_packets; ++i)
			{
				if (i > 0)
				{
					fprintf(out_file, ", ");
				}

				if (packet_received_ts[i].QuadPart)
				{
					fprintf(out_file, "%f", (double)(packet_received_ts[i].QuadPart - packet_sent_ts[i].QuadPart) / (double)clock_frequency.QuadPart);
				}
				else
				{
					fprintf(out_file, "-1.0");
				}
			}

			fprintf(out_file, "]\n\t\t}");

			// next config
			++test_config;
		}	
	}

	fprintf(out_file, "\n\t]\n}\n");
	fflush(out_file);


    return 0;
}

