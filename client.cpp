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

void record_and_send(int deviceIndex) {
  // Инициализация Opus кодека
  logMessage("Initializing Opus encoder.");
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
  int buffer[FRAMES_PER_BUFFER * NUM_CHANNELS] = {
      0};  // буфер для хранения данных в исходном виде
  unsigned char opus_data[OPUS_MAX_PACKET_SIZE];  // буфер для хранения данных в
                                                  // закодированном виде

  err = Pa_Initialize();
  if (err != paNoError) {
    std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
    return;
  }

  inputParameters.device = Pa_GetDefaultInputDevice();  // выбираем устройство
  if (inputParameters.device == paNoDevice) {
    std::cerr << "Error: No default input device." << std::endl;
    Pa_Terminate();
    return;
  }
  inputParameters.device = deviceIndex;  // устанавливаем нужное устройство
  inputParameters.channelCount = NUM_CHANNELS;
  inputParameters.sampleFormat = SAMPLE_TYPE;  // выбираем тип данных
  inputParameters.suggestedLatency =
      Pa_GetDeviceInfo(inputParameters.device)->defaultLowInputLatency;
  inputParameters.hostApiSpecificStreamInfo = NULL;

  err = Pa_OpenStream(&stream, &inputParameters, NULL, SAMPLE_RATE,
                      FRAMES_PER_BUFFER, paClipOff, NULL,
                      NULL);  // открываем поток
  if (err != paNoError) {
    std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
    Pa_Terminate();
    return;
  }

  err = Pa_StartStream(stream);  // запускаем поток
  if (err != paNoError) {
    std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
    Pa_CloseStream(stream);
    Pa_Terminate();
    return;
  }
  logMessage("Sending audio data.");
  while (client_running) {
    err = Pa_ReadStream(stream, buffer, FRAMES_PER_BUFFER);  // читаем данные
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
  Pa_Terminate();                 // Завершаем работу PortAudio
  opus_encoder_destroy(encoder);  // Освобождаем ресурсы Opus
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

void receive_and_play() {
  int error;
  OpusDecoder *decoder = opus_decoder_create(SAMPLE_RATE, NUM_CHANNELS, &error);
  if (error != OPUS_OK) {
    std::cerr << "Failed to create Opus decoder: " << opus_strerror(error)
              << std::endl;
    close(client_socket);
    return;
  }
  char opus_data[OPUS_MAX_PACKET_SIZE];
  std::vector<int16_t> audioData;

  while (client_running) {
    logMessage("Receiving audio data.");
    audioData.clear();
    int received = recv(client_socket, opus_data, sizeof(opus_data), 0);
    if (received <= 0) {
      std::cerr << "Client disconnected or error occurred." << std::endl;
      close(client_socket);
      break;
    }
    int16_t decoded_data[FRAMES_PER_BUFFER * NUM_CHANNELS];
    int frame_count = opus_decode(decoder, (const unsigned char *)opus_data,
                                  received, decoded_data, FRAMES_PER_BUFFER, 0);
    if (frame_count < 0) {
      std::cerr << "Opus decoding error: " << opus_strerror(frame_count)
                << std::endl;
      break;
    }

    audioData.insert(audioData.end(), decoded_data,
                     decoded_data + frame_count * NUM_CHANNELS);
    play_audio(audioData);
  }

  opus_decoder_destroy(decoder);

  // Воспроизведение аудиофайла после завершения сессии клиента
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

  std::thread record_thread(record_and_send, 4);
  std::thread receive_thread(receive_and_play);
  record_thread.join();
  receive_thread.join();

  close(client_socket);
  std::cout << "Client shut down successfully." << std::endl;
  return 0;
}
