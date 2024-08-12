#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

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
#define SAMPLE_TYPE int16_t

std::atomic<bool> server_running(true);
int server_socket;

void logMessage(const std::string &message) {
  std::ofstream logFile("server.log", std::ios_base::app);
  logFile << message << std::endl;
  logFile.close();
}

void saveAudioDataToFile(const std::vector<SAMPLE_TYPE> &audioData) {
  // Открываем файл для записи
  std::ofstream outFile("recorded_audio.raw", std::ios::binary);

  // Проверяем, прошло ли успешное открытие файла
  if (!outFile) {
    std::cerr << "Error opening file for writing." << std::endl;
    return;
  }

  // Записываем аудиоданные в файл
  outFile.write(reinterpret_cast<const char *>(audioData.data()),
                audioData.size() * sizeof(SAMPLE_TYPE));

  // Закрываем файл
  outFile.close();

  int result = system(
      "ffmpeg -f s16le -ar 44100 -ac 2 -i recorded_audio.raw "
      "recorded_audio.wav");
  if (result != 0) {
    std::cerr << "Error converting raw file to wav format." << std::endl;
  }
}

/**
 *Обработка клиентского соединения и получение аудиоданных.
 *Сохраните полученные аудиоданные в файл после завершения записи.
 *
 *@param client_socket Дескриптор файла сокета клиента.
 */
void handle_client(int client_socket) {
  // Буфер для приема аудиокадров
  char buffer[FRAMES_PER_BUFFER * NUM_CHANNELS * sizeof(SAMPLE_TYPE)];

  // Вектор для хранения аудиоданных
  std::vector<SAMPLE_TYPE> audioData;

  // Цикл до тех пор, пока сервер не перестанет работать или клиент не
  // отключится
  while (server_running) {
    // Получаем аудиокадры от клиента
    int received = recv(client_socket, buffer, sizeof(buffer), 0);

    // Если больше не получено кадров или произошла ошибка, закрываем клиентский
    // сокет
    if (received <= 0) {
      logMessage("Client disconnected or error occurred.");
      close(client_socket);
      break;
    }

    // Вставляем полученные аудиокадры в вектор аудиоданных
    audioData.insert(audioData.end(), (SAMPLE_TYPE *)buffer,
                     (SAMPLE_TYPE *)(buffer + received / sizeof(SAMPLE_TYPE)));
  }

  // Сохраняем аудиоданные в файл
  saveAudioDataToFile(audioData);
}

/**
 *Сохранение аудиоданных в файл.
 *
 *@param audioData Вектор, содержащий аудиоданные.
 */

void signal_handler(int signal) {
  if (signal == SIGINT) {
    logMessage("Interrupt signal (" + std::to_string(signal) +
               ") received. Shutting down server...");
    std::cout << "Shutting down server..." << std::endl;
    server_running = false;
    close(server_socket);  // Разблокируем принятие()
  }
}

/**
 *@brief Основная функция, которая настраивает сокет сервера и прослушивает
 *клиента связи. Когда клиент подключается, он создает новый поток для обработки
 *подключение клиента. Сервер продолжает прослушивать новые соединения до тех
 *пор, пока не сервер закрыт.
 *
 *@return int Возвращает 0 при успешном завершении работы.
 */
int main() {
  // Создаём сокет для сервера
  struct sockaddr_in address;
  int opt = 1;  // Вариант повторного использования сокета
  int addrlen = sizeof(address);  // Длина структуры адреса

  // Настраиваем обработчик сигнала прерывания
  signal(SIGINT, signal_handler);

  // Создаем сокет
  if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
    perror("socket failed");
    logMessage("Socket creation failed");
    exit(EXIT_FAILURE);
  }

  // Устанавливаем параметры сокета
  if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt,
                 sizeof(opt))) {
    perror("setsockopt");
    logMessage("Set socket options failed");
    close(server_socket);
    exit(EXIT_FAILURE);
  }

  // Устанавливаем структуру адреса
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(4444);

  // Привязать сокет к адресу
  if (bind(server_socket, (struct sockaddr *)&address, sizeof(address)) < 0) {
    perror("bind failed");
    logMessage("Bind failed");
    close(server_socket);
    exit(EXIT_FAILURE);
  }

  // Прослушиваем входящие соединения
  if (listen(server_socket, 3) < 0) {
    perror("listen");
    logMessage("Listen failed");
    close(server_socket);
    exit(EXIT_FAILURE);
  }

  // Вектор для хранения клиентских потоков
  std::vector<std::thread> client_threads;

  // Начинаем прослушивать входящие соединения
  while (server_running) {
    sockaddr_in client_address;
    socklen_t client_size = sizeof(client_address);
    int client_socket =
        accept(server_socket, (struct sockaddr *)&client_address, &client_size);

    // Проверяем, работает ли сервер
    if (!server_running) {
      close(client_socket);
      break;
    }

    // Проверяем, действителен ли клиентский сокет
    if (client_socket < 0) {
      perror("accept");
      logMessage("Accept failed");
      if (server_running) {
        close(server_socket);
        exit(EXIT_FAILURE);
      }

    } else {
      logMessage("Connection established");
      // Создаем новый поток для обработки клиентского соединения
      client_threads.emplace_back(handle_client, client_socket);
    }
  }

  // Ждем завершения всех клиентских потоков
  for (auto &thread : client_threads) {
    if (thread.joinable()) {
      thread.join();
    }
  }

  // Закрываем сокет сервера
  close(server_socket);
  std::cout << "Server shut down successfully." << std::endl;
  return 0;
}
