# Компилятор и флаги
CXX = g++

all: clean server client

# Целевые исполняемые файлы
server: server.cpp 
	$(CXX) -o server server.cpp

client: client.cpp
	$(CXX) -o client client.cpp -lportaudio

# Удаление текстовых файлов и собранных программ
clean:
	rm -f server client server.log
	rm -f *.o
