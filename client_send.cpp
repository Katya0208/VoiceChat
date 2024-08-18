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

void record_audio() {
  int error;
  OpusEncoder *encoder = opus_encoder_create(SAMPLE_RATE, NUM_CHANNELS,
                                             OPUS_APPLICATION_VOIP, &error);
  if (error != OPUS_OK) {
    std::cerr << "Failed to create Opus encoder: " << opus_strerror(error)
              << std::endl;
    return;
  }
  PaStream *stream;
  PaError err;
  short buffer[FRAMES_PER_BUFFER * NUM_CHANNELS];
  unsigned char opus_data[OPUS_MAX_PACKET_SIZE];

  err = Pa_Initialize();
  if (err != paNoError) {
    std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
    return;
  }

  PaStreamParameters inputParameters;
  inputParameters.device = Pa_GetDefaultInputDevice();
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

  std::cout << "Recording started..." << std::endl;
  // audio_buffer.clear();  // Очищаем буфер перед записью

  while (recording_start) {
    err = Pa_ReadStream(stream, buffer, FRAMES_PER_BUFFER);
    if (err && err != paInputOverflowed) {
      std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
      break;
    }

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

    // Сохраняем данные в буфер
    // audio_buffer.insert(audio_buffer.end(), buffer,
    //                     buffer + FRAMES_PER_BUFFER * NUM_CHANNELS);
  }

  Pa_StopStream(stream);
  Pa_CloseStream(stream);
  Pa_Terminate();

  std::cout << "Recording stopped." << std::endl;
}

void record_and_send(int deviceIndex) {
  std::string input;
  if (input.empty()) {
  }
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
  inputParameters.device = deviceIndex;
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
  server_address.sin_port = htons(50505);
  server_address.sin_addr.s_addr = INADDR_ANY;

  if (connect(client_socket, (struct sockaddr *)&server_address,
              sizeof(server_address)) == -1) {
    std::cerr << "Failed to connect to server." << std::endl;
    return 2;
  }

  while (true) {
    std::string input;
    std::getline(std::cin, input);

    if (input.empty()) {
      if (!recording_start) {
        // Начинаем запись
        recording_start = true;
        std::thread record_thread(record_audio);
        record_thread.detach();
      } else {
        // Останавливаем запись
        recording_start = false;
        send(client_socket, "\0", 1, 0);
        // save_audio_to_file("recording.wav");
      }
    } else {
      std::cout << "Press ENTER to start/stop recording..." << std::endl;
    }
  }

  // std::thread keypress_thread(enter_pressed);
  // std::thread record_thread(record_and_send, 4);
  // keypress_thread.join();
  // record_thread.join();

  close(client_socket);
  std::cout << "Client shut down successfully." << std::endl;
  return 0;
}
