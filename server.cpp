#include <netinet/in.h>
#include <opus/opus.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <csignal>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

#define SAMPLE_RATE 48000
#define FRAMES_PER_BUFFER 480
#define NUM_CHANNELS 2
#define SAMPLE_TYPE int16_t
#define OPUS_MAX_PACKET_SIZE 4000

std::atomic<bool> server_running(true);
int server_socket;

void saveAudioDataToFile(const std::vector<SAMPLE_TYPE> &audioData) {
  std::ofstream outFile("recorded_audio.raw", std::ios::binary);
  if (!outFile) {
    std::cerr << "Error opening file for writing." << std::endl;
    return;
  }

  outFile.write(reinterpret_cast<const char *>(audioData.data()),
                audioData.size() * sizeof(SAMPLE_TYPE));
  outFile.close();

  int result = system(
      "ffmpeg -f s16le -ar 44100 -ac 2 -i recorded_audio.raw "
      "recorded_audio.wav");
  if (result != 0) {
    std::cerr << "Error converting raw file to wav format." << std::endl;
  }
}

void handle_client(int client_socket) {
  char opus_data[OPUS_MAX_PACKET_SIZE];
  std::vector<SAMPLE_TYPE> audioData;

  // Инициализация Opus декодера
  int error;
  OpusDecoder *decoder = opus_decoder_create(SAMPLE_RATE, NUM_CHANNELS, &error);
  if (error != OPUS_OK) {
    std::cerr << "Failed to create Opus decoder: " << opus_strerror(error)
              << std::endl;
    close(client_socket);
    return;
  }

  while (server_running) {
    int received = recv(client_socket, opus_data, sizeof(opus_data), 0);
    if (received <= 0) {
      std::cerr << "Client disconnected or error occurred." << std::endl;
      close(client_socket);
      break;
    }

    SAMPLE_TYPE decoded_data[FRAMES_PER_BUFFER * NUM_CHANNELS];
    int frame_count = opus_decode(decoder, (const unsigned char *)opus_data,
                                  received, decoded_data, FRAMES_PER_BUFFER, 0);
    if (frame_count < 0) {
      std::cerr << "Opus decoding error: " << opus_strerror(frame_count)
                << std::endl;
      break;
    }

    audioData.insert(audioData.end(), decoded_data,
                     decoded_data + frame_count * NUM_CHANNELS);
  }

  opus_decoder_destroy(decoder);
  saveAudioDataToFile(audioData);
}

void signal_handler(int signal) {
  if (signal == SIGINT) {
    std::cout << "Shutting down server..." << std::endl;
    server_running = false;
    close(server_socket);
  }
}

int main() {
  signal(SIGINT, signal_handler);

  server_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (server_socket == -1) {
    std::cerr << "Failed to create socket." << std::endl;
    return 1;
  }

  struct sockaddr_in server_address;
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = INADDR_ANY;
  server_address.sin_port = htons(4444);

  if (bind(server_socket, (struct sockaddr *)&server_address,
           sizeof(server_address)) < 0) {
    std::cerr << "Bind failed." << std::endl;
    close(server_socket);
    return 2;
  }

  if (listen(server_socket, 3) < 0) {
    std::cerr << "Listen failed." << std::endl;
    close(server_socket);
    return 3;
  }

  while (server_running) {
    int client_socket = accept(server_socket, NULL, NULL);
    if (client_socket >= 0) {
      std::thread(handle_client, client_socket).detach();
    }
  }

  close(server_socket);
  std::cout << "Server shut down successfully." << std::endl;
  return 0;
}
