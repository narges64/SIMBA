# Makefile for ssdsim 

EXEC = ssd

OBJ_FILES = \
	common.o ftl.o flash.o\
	garbage_collection.o ssd.o\

OBJ_DIR = obj
OBJS = $(patsubst %.o,$(OBJ_DIR)/%.o,$(OBJ_FILES))

CC = g++
CFLAGS = -ggdb -I. -std=c++11 -pg 

$(EXEC): $(OBJS)
	$(CC) -o $@ $^ $(CFLAGS)

$(OBJ_DIR)/%.o: %.cpp	%.hh 
	@mkdir -p $(OBJ_DIR)
	$(CC) -c -o $@ $< $(CFLAGS)


debug: CFLAGS += -DDEBUG -ggdb -pg -std=c++11 
debug: $(EXEC)

clean:
	rm -rf $(OBJ_DIR) *.bak $(EXEC) *~
