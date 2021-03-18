TARGET=webserver
SRC=http_conn.o main.o sockio.o 

$(TARGET):$(SRC)
	g++ $(SRC) -o $(TARGET) -pthread
%.o:%.cpp
	g++ -c $< -o $@ -pthread