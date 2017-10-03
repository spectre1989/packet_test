#pragma once

#include <stdint.h>
#include <Windows.h>


#define assert(x) if(!(x)) { int* p = 0; *p = 0; }


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


float time_since_s(LARGE_INTEGER* t, LARGE_INTEGER* freq);
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
uint32_t create_capture_started_packet(char* buffer);
uint32_t create_results_packet(char* buffer, uint32_t batch_id, uint32_t batch_start, uint32_t num_batches, 
								uint32_t* packet_ids, LARGE_INTEGER* packet_ts, uint32_t packet_count);
void read_results_packet(char* buffer, uint32_t packet_size, uint32_t* batch_id, uint32_t* num_batches, 
								uint32_t* packet_ids, LARGE_INTEGER* packet_ts);
