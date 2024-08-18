#include <portaudio.h>
#include <sndfile.h>

#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#define SAMPLE_RATE 44100
#define FRAMES_PER_BUFFER 512
#define NUM_CHANNELS 1
#define SAMPLE_FORMAT paInt16
#define FILE_FORMAT (SF_FORMAT_WAV | SF_FORMAT_PCM_16)

bool recording_start = false;
std::vector<short> audio_buffer;  // Буфер для хранения записанных данных

void record_audio() {
  PaStream *stream;
  PaError err;
  short buffer[FRAMES_PER_BUFFER * NUM_CHANNELS];

  err = Pa_Initialize();
  if (err != paNoError) {
    std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
    return;
  }

  PaStreamParameters inputParameters;
  inputParameters.device = Pa_GetDefaultInputDevice();
  inputParameters.channelCount = NUM_CHANNELS;
  inputParameters.sampleFormat = SAMPLE_FORMAT;
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
  audio_buffer.clear();  // Очищаем буфер перед записью

  while (recording_start) {
    err = Pa_ReadStream(stream, buffer, FRAMES_PER_BUFFER);
    if (err && err != paInputOverflowed) {
      std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
      break;
    }

    // Сохраняем данные в буфер
    audio_buffer.insert(audio_buffer.end(), buffer,
                        buffer + FRAMES_PER_BUFFER * NUM_CHANNELS);
  }

  Pa_StopStream(stream);
  Pa_CloseStream(stream);
  Pa_Terminate();

  std::cout << "Recording stopped." << std::endl;
}

void save_audio_to_file(const std::string &filename) {
  SF_INFO sfinfo;
  sfinfo.channels = NUM_CHANNELS;
  sfinfo.samplerate = SAMPLE_RATE;
  sfinfo.format = FILE_FORMAT;

  SNDFILE *outfile = sf_open(filename.c_str(), SFM_WRITE, &sfinfo);
  if (!outfile) {
    std::cerr << "Error opening file: " << sf_strerror(NULL) << std::endl;
    return;
  }

  sf_write_short(outfile, audio_buffer.data(), audio_buffer.size());
  sf_close(outfile);

  std::cout << "Audio saved to " << filename << std::endl;
}

int main() {
  std::cout << "Press ENTER to start/stop recording..." << std::endl;

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
        save_audio_to_file("recording.wav");
      }
    } else {
      std::cout << "Press ENTER to start/stop recording..." << std::endl;
    }
  }

  return 0;
}
