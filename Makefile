# Компилятор и флаги
CXX = g++

all: clean server client_listen client_send

# Целевые исполняемые файлы
server: server.cpp 
	$(CXX) -o server server.cpp -lopus -lportaudio

client_listen: client_listen.cpp
	$(CXX) -o client_listen client_listen.cpp -lportaudio -lopus

client_send: client_send.cpp
	$(CXX) -o client_send client_send.cpp -lportaudio -lopus

# Удаление текстовых файлов и собранных программ
clean:
	rm -f server client_send client_listen *.log recorded_audio.raw recorded_audio.wav
	rm -f *.o
