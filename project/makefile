TARFET=server
TARGET=client
OBJ=chatRoomServer.o
OBJECT=chatRoomClient.o

LDFALGS=-L./src_so -L./src_so2
LIBS=-lAVLTree -lMythreadPool

$(TARFET):$(OBJ)
	$(CC) -g $^ $(LIBS) $(LDFALGS) -o $@ -lsqlite3

$(TARGET):$(OBJECT)
	$(CC) -g $^ $(LIBS) $(LDFALGS) -o $@ -lsqlite3

%.o:%.c
	gcc -c $^ -o $@

# 伪目标
.PHONY:clean

clean:
	@rm -rf *.o $(TARFET) $(TARGET)

all:server client