#include <netinet/in.h>
#include <opus/opus.h>
#include <portaudio.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <csignal>
#include <cstring>
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

void signal_handler(int signal) {
  if (signal == SIGINT) {
    std::cout << "Shutting down client..." << std::endl;
    client_running = false;
    close(client_socket);
    exit(EXIT_SUCCESS);
  }
}

void record_and_send() {
  // Инициализация Opus кодека
  int error;
  OpusEncoder *encoder = opus_encoder_create(SAMPLE_RATE, NUM_CHANNELS,
                                             OPUS_APPLICATION_VOIP, &error);
  if (error != OPUS_OK) {
    std::cerr << "Failed to create Opus encoder: " << opus_strerror(error)
              << std::endl;
    return;
  }

  PaStreamParameters inputParameters;
  PaStream *stream;
  PaError err;
  int buffer[FRAMES_PER_BUFFER * NUM_CHANNELS] = {0};
  unsigned char opus_data[OPUS_MAX_PACKET_SIZE];

  err = Pa_Initialize();
  if (err != paNoError) {
    std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
    return;
  }

  inputParameters.device = Pa_GetDefaultInputDevice();
  if (inputParameters.device == paNoDevice) {
    std::cerr << "Error: No default input device." << std::endl;
    Pa_Terminate();
    return;
  }

  inputParameters.channelCount = NUM_CHANNELS;
  inputParameters.sampleFormat = SAMPLE_TYPE;
  inputParameters.suggestedLatency =
      Pa_GetDeviceInfo(inputParameters.device)->defaultLowInputLatency;
  inputParameters.hostApiSpecificStreamInfo = NULL;

  err = Pa_OpenStream(&stream, &inputParameters, NULL, SAMPLE_RATE,
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

  while (client_running) {
    err = Pa_ReadStream(stream, buffer, FRAMES_PER_BUFFER);
    if (err && err != paInputOverflowed) {
      std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
      break;
    }

    // Кодирование аудио данных с использованием Opus
    int opus_length =
        opus_encode(encoder, (const opus_int16 *)buffer, FRAMES_PER_BUFFER,
                    opus_data, OPUS_MAX_PACKET_SIZE);
    if (opus_length < 0) {
      std::cerr << "Opus encoding error: " << opus_strerror(opus_length)
                << std::endl;
      break;
    }

    // Отправка закодированных данных на сервер
    send(client_socket, opus_data, opus_length, 0);
  }

  Pa_StopStream(stream);
  Pa_CloseStream(stream);
  Pa_Terminate();
  opus_encoder_destroy(encoder);
}

int main() {
  signal(SIGINT, signal_handler);

  client_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (client_socket == -1) {
    std::cerr << "Failed to create socket." << std::endl;
    return 1;
  }

  sockaddr_in server_address;
  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(4444);
  server_address.sin_addr.s_addr = INADDR_ANY;

  if (connect(client_socket, (struct sockaddr *)&server_address,
              sizeof(server_address)) == -1) {
    std::cerr << "Failed to connect to server." << std::endl;
    return 2;
  }

  std::thread record_thread(record_and_send);
  record_thread.join();

  close(client_socket);
  std::cout << "Client shut down successfully." << std::endl;
  return 0;
}
