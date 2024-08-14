#include <netinet/in.h>
#include <opus/opus.h>
#include <portaudio.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

#define SAMPLE_RATE 48000
#define FRAMES_PER_BUFFER 480
#define NUM_CHANNELS 2
#define SAMPLE_TYPE paInt16
#define OPUS_MAX_PACKET_SIZE 4000

std::atomic<bool> client_running(true);
int client_socket;

void signal_handler(int signal);
void record_and_send(int deviceIndex);
void play_audio(const std::vector<int16_t> &audioData);
void receive_and_play();
