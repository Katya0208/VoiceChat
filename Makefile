# Компилятор и флаги
CXX = g++

all: clean server client

# Целевые исполняемые файлы
server: server.cpp 
	$(CXX) -o server server.cpp -lopus -lportaudio

client: client.cpp
	$(CXX) -o client client.cpp clientCounter.cpp -lportaudio -lopus

# Удаление текстовых файлов и собранных программ
clean:
	rm -f server client *.log recorded_audio.raw recorded_audio.wav
	rm -f *.o
