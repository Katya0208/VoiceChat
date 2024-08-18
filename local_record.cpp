#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <portaudio.h>

#include <atomic>
#include <csignal>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

#define SAMPLE_RATE 44100
#define FRAMES_PER_BUFFER 512
#define NUM_CHANNELS 2
#define SAMPLE_TYPE paInt16
#define SAMPLE_TYPE1 int16_t
typedef short SAMPLE;

std::atomic<bool> running(true);
std::atomic<bool> recording(false);

void signal_handler(int signal) {
  if (signal == SIGINT) {
    std::cout << "Shutting down recording..." << std::endl;
    running = false;
  }
}

void saveAudioDataToFile(const std::vector<SAMPLE_TYPE1> &audioData) {
  std::ofstream outFile("audio.raw", std::ios::binary);
  if (!outFile) {
    std::cerr << "Error opening file for writing." << std::endl;
    return;
  }
  outFile.write(reinterpret_cast<const char *>(audioData.data()),
                audioData.size() * sizeof(SAMPLE_TYPE1));
  outFile.close();

  int result = system("ffmpeg -f s16le -ar 44100 -ac 2 -i audio.raw audio.wav");
  if (result != 0) {
    std::cerr << "Error converting raw file to wav format." << std::endl;
  }
}

void record_and_save(int deviceIndex) {
  PaStreamParameters inputParameters;
  PaStream *stream = nullptr;
  PaError err;
  SAMPLE buffer[FRAMES_PER_BUFFER * NUM_CHANNELS];

  std::vector<SAMPLE_TYPE1> audioData;

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

  while (running) {
    if (recording) {
      std::cout << "RECORDING..." << std::endl;
      err = Pa_ReadStream(stream, buffer, FRAMES_PER_BUFFER);
      if (err && err != paInputOverflowed) {
        std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
        break;
      }
      audioData.insert(audioData.end(), buffer,
                       buffer + FRAMES_PER_BUFFER * NUM_CHANNELS);
    }
  }

  Pa_StopStream(stream);
  Pa_CloseStream(stream);
  Pa_Terminate();

  if (!audioData.empty()) {
    saveAudioDataToFile(audioData);
  }
}

void listen_for_keypress() {
  Display *display = XOpenDisplay(NULL);
  if (display == NULL) {
    std::cerr << "Cannot open X display" << std::endl;
    return;
  }

  Window root = DefaultRootWindow(display);
  XEvent event;
  while (running) {
    while (XPending(display)) {
      XNextEvent(display, &event);
      if (event.type == KeyPress) {
        KeySym keysym = XLookupKeysym(&event.xkey, 0);
        if (keysym == XK_space) {
          recording = true;
          std::cout << "Recording started..." << std::endl;
        }
      } else if (event.type == KeyRelease) {
        KeySym keysym = XLookupKeysym(&event.xkey, 0);
        if (keysym == XK_space) {
          recording = false;
          std::cout << "Recording stopped." << std::endl;
        }
      }
    }
    usleep(1000);  // Sleep for a short time to prevent high CPU usage
  }

  XCloseDisplay(display);
}

int main() {
  signal(SIGINT, signal_handler);

  std::thread record_thread(record_and_save, 4);
  std::thread keypress_thread(listen_for_keypress);

  record_thread.join();
  keypress_thread.join();

  std::cout << "Recording completed and saved to 'audio.wav'" << std::endl;
  return 0;
}
