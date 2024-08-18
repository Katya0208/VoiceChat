#include <netinet/in.h>
#include <opus/opus.h>
#include <portaudio.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <csignal>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#define SAMPLE_RATE 48000
#define FRAMES_PER_BUFFER 480
#define NUM_CHANNELS 2
#define SAMPLE_TYPE int16_t
#define OPUS_MAX_PACKET_SIZE 4000

std::atomic<bool> server_running(true);
int server_socket;

std::vector<int> client_sockets;  // Список подключенных клиентов
std::mutex clients_mutex;  // Мьютекс для защиты списка клиентов

void logMessage(const std::string &message) {
  std::ofstream logFile("server.log", std::ios_base::app);
  if (!logFile) {  // Проверка, удалось ли открыть файл
    std::cerr << "Failed to open log file." << std::endl;
    return;
  }
  logFile << message << std::endl;
  logFile.close();
}

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

void play_audio(const std::vector<SAMPLE_TYPE> &audioData) {
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

void broadcast_audio(const std::vector<SAMPLE_TYPE> &audioData,
                     int sender_socket) {
  logMessage("broadcast_audio");
  std::lock_guard<std::mutex> lock(clients_mutex);
  logMessage("Size: " + client_sockets.size());

  for (int client_socket : client_sockets) {
    if (client_socket != sender_socket) {
      logMessage("Send");
      std::cout << "Sending audio data to client "
                << std::to_string(client_socket) << "..." << std::endl;
      send(client_socket, audioData.data(),
           audioData.size() * sizeof(SAMPLE_TYPE), 0);
    }
  }
}
void handle_client(int client_socket) {
  logMessage("handle_client");
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

  {
    std::lock_guard<std::mutex> lock(clients_mutex);
    client_sockets.push_back(client_socket);
  }

  while (server_running) {
    int received = recv(client_socket, opus_data, sizeof(opus_data), 0);
    if (received <= 0) {
      if (received == 0) {
        std::cout << "Client disconnected." << std::endl;
      } else {
        std::cerr << "Error receiving data from client." << std::endl;
      }
      break;
    }
    logMessage("Received messages from send-client");
    if (received == 1 && opus_data[0] == '\0') {
      logMessage("End message from send-client");
      // Специальный сигнал от клиента, что запись завершена
      // saveAudioDataToFile(audioData);
      broadcast_audio(audioData, client_socket);
      audioData.clear();  // Очищаем буфер для следующей записи
    } else {
      SAMPLE_TYPE decoded_data[FRAMES_PER_BUFFER * NUM_CHANNELS];
      int frame_count =
          opus_decode(decoder, (const unsigned char *)opus_data, received,
                      decoded_data, FRAMES_PER_BUFFER, 0);
      if (frame_count < 0) {
        std::cerr << "Opus decoding error: " << opus_strerror(frame_count)
                  << std::endl;
        break;
      }

      // Сохраняем данные в буфер
      audioData.insert(audioData.end(), decoded_data,
                       decoded_data + frame_count * NUM_CHANNELS);
    }
  }

  // Освобождаем ресурсы Opus
  opus_decoder_destroy(decoder);

  {
    std::lock_guard<std::mutex> lock(clients_mutex);
    for (auto it = client_sockets.begin(); it != client_sockets.end(); ++it) {
      if (*it == client_socket) {
        client_sockets.erase(it);
        break;  // Прерываем цикл, так как мы уже нашли и удалили нужный элемент
      }
    }
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
  server_address.sin_port = htons(50505);

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
