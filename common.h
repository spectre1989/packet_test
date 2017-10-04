#pragma once

#include <stdint.h>
#include <Windows.h>


#define assert(x) if(!(x)) { int* p = 0; *p = 0; }


const uint16_t c_port = 9876;
const uint32_t c_batch_size_in_bytes = 1024;
const uint32_t c_batch_header_size_in_bytes = 13;
const uint32_t c_bytes_per_result = 12;
const uint32_t c_num_results_per_batch = (c_batch_size_in_bytes - c_batch_header_size_in_bytes) / c_bytes_per_result;


enum Client_Msg : uint8_t
{
	Start_Capture,
	Test_Packet,
	End_Capture,
	Ack_Results
};

enum Server_Msg : uint8_t
{
	Capture_Started,
	Results
};


float time_since_s(LARGE_INTEGER t, LARGE_INTEGER freq);
uint32_t num_batches_needed_for_num_results(uint32_t num_results);
void send_packet(SOCKET sock, char* buffer, int buffer_size, sockaddr_in* address);
int receive_packet(SOCKET sock, char* buffer, int buffer_size, sockaddr_in* address);
// client msgs
uint32_t create_start_capture_packet(char* buffer, uint32_t num_packets);
void read_start_capture_packet(char* buffer, uint32_t* num_packets);
uint32_t create_test_packet(char* buffer, uint32_t id, uint32_t size);
void read_test_packet(char* buffer, uint32_t* id);
uint32_t create_end_capture_packet(char* buffer);
uint32_t create_ack_results_packet(char* buffer, uint32_t batch_id);
void read_ack_results_packet(char* buffer, uint32_t* batch_id);
// server msgs
uint32_t create_capture_started_packet(char* buffer, LARGE_INTEGER clock_frequency);
void read_capture_started_packet(char* buffer, LARGE_INTEGER* server_clock_frequency);
uint32_t create_results_packet(char* buffer, uint32_t batch_id, uint32_t batch_start, uint32_t num_batches, uint32_t max_results_per_batch,
								uint32_t* results_ids, LARGE_INTEGER* results_ts, uint32_t results_count);
void read_results_packet_header(char* buffer, uint32_t* batch_id, uint32_t* num_batches);
void read_results_packet_body(char* buffer, uint32_t packet_size, uint32_t* packet_ids, LARGE_INTEGER* packet_ts);
