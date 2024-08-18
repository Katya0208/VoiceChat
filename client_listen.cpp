#include "client.hpp"

void logMessage(const std::string &message) {
  std::ofstream logFile("client.log", std::ios_base::app);
  logFile << message << std::endl;
  logFile.close();
}

void signal_handler(int signal) {
  if (signal == SIGINT) {
    std::cout << "Shutting down client..." << std::endl;
    client_running = false;
    close(client_socket);
    exit(EXIT_SUCCESS);
  }
}

void play_audio(const std::vector<int16_t> &audioData) {
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

  logMessage("Playing audio data.");

  err =
      Pa_WriteStream(stream, audioData.data(), audioData.size() / NUM_CHANNELS);
  if (err != paNoError) {
    std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
  }

  Pa_StopStream(stream);
  Pa_CloseStream(stream);
  Pa_Terminate();
}

void receive_and_play() {
  std::vector<int16_t> audioData;
  char buffer[1024];

  while (client_running) {
    // Получаем количество байт аудио
    int received = recv(client_socket, buffer, sizeof(buffer), 0);
    if (received <= 0) {
      std::cerr << "Server disconnected or error occurred." << std::endl;
      break;
    }

    // Добавляем полученные данные в буфер
    audioData.insert(audioData.end(), (int16_t *)buffer,
                     (int16_t *)(buffer + received));

    // Проверяем, завершена ли текущая сессия передачи данных
    if (received < sizeof(buffer)) {
      // Предполагаем, что сервер завершил передачу данных сессии
      play_audio(audioData);  // Воспроизводим полученное аудио
      audioData.clear();  // Очищаем буфер для следующей сессии
    }
  }
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
  server_address.sin_port = htons(50505);
  server_address.sin_addr.s_addr = INADDR_ANY;

  if (connect(client_socket, (struct sockaddr *)&server_address,
              sizeof(server_address)) == -1) {
    std::cerr << "Failed to connect to server." << std::endl;
    return 2;
  }

  std::cout << "Connected to server. Waiting for audio..." << std::endl;

  std::thread receive_thread(receive_and_play);
  receive_thread.join();

  close(client_socket);
  std::cout << "Client shut down successfully." << std::endl;
  return 0;
}
