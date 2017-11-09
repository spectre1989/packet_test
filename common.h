#pragma once

#include <stdint.h>
#include <Windows.h>


#define assert(x) if(!(x)) { int* p = 0; *p = 0; }


const uint16_t c_port = 9876;
const uint32_t c_max_packet_size = 2048;


enum Client_Msg : uint8_t
{
	Start_Test,
	Test_Packet,
	End_Test
};

enum Server_Msg : uint8_t
{
	Test_Started,
	Test_Packet_Echo,
	Results
};


float time_since_s(LARGE_INTEGER t, LARGE_INTEGER freq);
void send_packet(SOCKET sock, char* buffer, int buffer_size, sockaddr_in* address);
int receive_packet(SOCKET sock, char* buffer, int buffer_size, sockaddr_in* address);
// client msgs
uint32_t create_start_test_packet(char* buffer, uint32_t num_packets);
void read_start_test_packet(char* buffer, uint32_t* num_packets);
uint32_t create_test_packet(char* buffer, uint32_t id, uint32_t size);
void read_test_packet(char* buffer, uint32_t* id);
uint32_t create_end_test_packet(char* buffer);
// server msgs
uint32_t create_test_started_packet(char* buffer);
void create_test_packet_echo(char* original_packet);
void read_test_packet_echo(char* buffer, uint32_t* id);
uint32_t create_results_packet(char* buffer, uint32_t num_dropped_packets, uint32_t num_duplicated_packets);
void read_results_packet(char* buffer, uint32_t* num_dropped_packets, uint32_t* num_duplicated_packets);
