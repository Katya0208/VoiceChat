#include <netinet/in.h>
#include <opus/opus.h>
#include <portaudio.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
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
std::vector<int> clients;
int server_socket;

void logMessage(const std::string &message) {
  std::ofstream logFile("server.log", std::ios_base::app);
  logFile << message << std::endl;
  logFile.close();
}
void saveAudioDataToFile(const std::vector<SAMPLE_TYPE> &audioData) {
  logMessage("Saving audio data to file.");
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

void play_audio(const std::vector<SAMPLE_TYPE> &audioData) {
  logMessage("Playing audio data.");
  PaError err;
  PaStream *stream;

  err = Pa_Initialize();
  if (err != paNoError) {
    std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
    return;
  }

  PaStreamParameters outputParameters;
  outputParameters.device = Pa_GetDefaultOutputDevice();
  if (outputParameters.device == paNoDevice) {
    std::cerr << "Error: No default output device." << std::endl;
    Pa_Terminate();
    return;
  }
  outputParameters.channelCount = NUM_CHANNELS;
  outputParameters.sampleFormat = paInt16;
  outputParameters.suggestedLatency =
      Pa_GetDeviceInfo(outputParameters.device)->defaultLowOutputLatency;
  outputParameters.hostApiSpecificStreamInfo = NULL;

  err = Pa_OpenStream(&stream, NULL, &outputParameters, SAMPLE_RATE,
                      FRAMES_PER_BUFFER, paClipOff, NULL, NULL);
  if (err != paNoError) {
    std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
    Pa_Terminate();
    return;
  }

  err = Pa_StartStream(stream);
  if (err != paNoError) {
    std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
    Pa_CloseStream(stream);
    Pa_Terminate();
    return;
  }

  // Воспроизведение аудиоданных
  err =
      Pa_WriteStream(stream, audioData.data(), audioData.size() / NUM_CHANNELS);
  if (err != paNoError) {
    std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
  }

  Pa_StopStream(stream);
  Pa_CloseStream(stream);
  Pa_Terminate();
}

void receive_data(int &client_socket, char *opus_data, int &received) {
  logMessage("Receiving data.");
  while (server_running) {
    received = recv(client_socket, opus_data, sizeof(opus_data), 0);
    if (received <= 0) {
      std::cerr << "Client disconnected or error occurred." << std::endl;
      close(client_socket);
      break;
    }
  }
}

void send_data(int &client_socket, char *opus_data, int &received) {
  logMessage("Sending data.");
  while (server_running) {
    for (int other_client_socket : clients) {
      if (other_client_socket != client_socket) {
        send(other_client_socket, opus_data, received, 0);
      }
    }
  }
}

void handle_client(int client_socket) {
  logMessage("Add new client.");
  clients.push_back(client_socket);

  char opus_data[OPUS_MAX_PACKET_SIZE];
  int received;

  while (server_running) {
    receive_data(client_socket, opus_data, received);
    send_data(client_socket, opus_data, received);
  }

  auto it = std::find(clients.begin(), clients.end(), client_socket);
  if (it != clients.end()) {
    clients.erase(it);
  }

  close(client_socket);
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
