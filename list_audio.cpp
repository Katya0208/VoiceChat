#include <portaudio.h>

#include <iostream>

int main() {
  Pa_Initialize();
  int numDevices = Pa_GetDeviceCount();
  const PaDeviceInfo *deviceInfo;

  for (int i = 0; i < numDevices; i++) {
    deviceInfo = Pa_GetDeviceInfo(i);
    std::cout << "Device #" << i << ": " << deviceInfo->name << " ("
              << ((deviceInfo->maxInputChannels > 0) ? "Input" : "")
              << ((deviceInfo->maxInputChannels > 0 &&
                   deviceInfo->maxOutputChannels > 0)
                      ? ", "
                      : "")
              << ((deviceInfo->maxOutputChannels > 0) ? "Output" : "") << ")"
              << std::endl;
  }

  Pa_Terminate();
  return 0;
}
