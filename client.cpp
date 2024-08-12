#include <netinet/in.h>
#include <portaudio.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <csignal>
#include <cstring>
#include <iostream>
#include <thread>

#define SAMPLE_RATE 44100
#define FRAMES_PER_BUFFER 512
#define NUM_CHANNELS 2
#define SAMPLE_TYPE paInt16
typedef short SAMPLE;

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

void receive_messages() {
  char buffer[1024];
  while (client_running) {
    memset(buffer, 0, sizeof(buffer));
    ssize_t bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
    if (bytes_received <= 0) {
      std::cerr << "Server disconnected or error occurred." << std::endl;
      client_running = false;
      break;
    }
    std::cout << "Server: " << buffer << std::endl;
  }
  close(client_socket);
}

/**
 *Записывает звук с устройства ввода по умолчанию и отправляет его на сервер
 *через данный сокет.
 */
void record_and_send() {
  // Инициализируем ПортАудио
  PaStreamParameters inputParameters;
  PaStream *stream;
  PaError err;
  SAMPLE buffer[FRAMES_PER_BUFFER * NUM_CHANNELS];

  err = Pa_Initialize();
  if (err != paNoError) {
    // Распечатываем сообщение об ошибке PortAudio и выходим
    std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
    return;
  }

  // Получаем устройство ввода по умолчанию
  inputParameters.device = Pa_GetDefaultInputDevice();
  if (inputParameters.device == paNoDevice) {
    // Выводим сообщение об ошибке и выходим
    std::cerr << "Error: No default input device." << std::endl;
    Pa_Terminate();
    return;
  }

  // Устанавливаем входные параметры
  inputParameters.channelCount = NUM_CHANNELS;
  inputParameters.sampleFormat = SAMPLE_TYPE;
  inputParameters.suggestedLatency =
      Pa_GetDeviceInfo(inputParameters.device)->defaultLowInputLatency;
  inputParameters.hostApiSpecificStreamInfo = NULL;

  // Открыть поток PortAudio
  err = Pa_OpenStream(&stream, &inputParameters, NULL, SAMPLE_RATE,
                      FRAMES_PER_BUFFER, paClipOff, NULL, NULL);
  if (err != paNoError) {
    // Распечатываем сообщение об ошибке PortAudio и выходим
    std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
    Pa_Terminate();
    return;
  }

  // Начинаем запись и отправку звука
  err = Pa_StartStream(stream);
  if (err != paNoError) {
    // Распечатываем сообщение об ошибке PortAudio и выходим
    std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
    Pa_CloseStream(stream);
    Pa_Terminate();
    return;
  }

  while (client_running) {
    // Читаем аудио с устройства ввода
    err = Pa_ReadStream(stream, buffer, FRAMES_PER_BUFFER);
    if (err && err != paInputOverflowed) {
      // Распечатываем сообщение об ошибке PortAudio и выходим
      std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
      break;
    }

    // Отправляем аудио на сервер
    send(client_socket, buffer, sizeof(buffer), 0);
  }

  // Остановим и закроем поток PortAudio
  Pa_StopStream(stream);
  Pa_CloseStream(stream);

  // Завершение работы PortAudio
  Pa_Terminate();
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

  std::thread receive_thread(receive_messages);
  std::thread record_thread(record_and_send);

  receive_thread.join();
  record_thread.join();

  close(client_socket);
  std::cout << "Client shut down successfully." << std::endl;
  return 0;
}
