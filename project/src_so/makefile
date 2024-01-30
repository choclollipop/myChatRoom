TARGET=libAVLTree.so
OBJ=$(patsubst %.c, %.o, $(wildcard ./*.c))

$(TARGET):$(OBJ)
	$(CC) -shared -fPIC $^ -o $@
	sudo cp $(TARGET) /usr/lib/

%.o:%.c
	$(CC) -c $^ -o $@

clean:
	@rm -rf *.o $(TARGET)
	sudo rm -rf /usr/lib/$(TARGET)