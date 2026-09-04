CC = gcc
CFLAGS = -Wvla -Wextra -Werror -D_GNU_SOURCE -O2
LDFLAGS = 

SRC_DIR = src
INC_DIR = inc
OBJ_DIR = obj
BIN_DIR = bin

EXECS = $(BIN_DIR)/responsabile $(BIN_DIR)/operatore $(BIN_DIR)/cassiere $(BIN_DIR)/utente

all: $(EXECS)

$(BIN_DIR)/responsabile: $(OBJ_DIR)/responsabile.o $(OBJ_DIR)/config_parser.o
	$(CC) $(CFLAGS) -o $@ $^

$(BIN_DIR)/operatore: $(OBJ_DIR)/operatore.o $(OBJ_DIR)/config_parser.o
	$(CC) $(CFLAGS) -o $@ $^

$(BIN_DIR)/cassiere: $(OBJ_DIR)/cassiere.o $(OBJ_DIR)/config_parser.o
	$(CC) $(CFLAGS) -o $@ $^

$(BIN_DIR)/utente: $(OBJ_DIR)/utente.o $(OBJ_DIR)/config_parser.o
	$(CC) $(CFLAGS) -o $@ $^

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR) $(BIN_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

test_timeout: $(EXECS)
	@echo "\n=== AVVIO TEST: SCENARIO TIMEOUT ==="
	./$(BIN_DIR)/responsabile "config timeout.conf"

test_overload: $(EXECS)
	@echo "\n=== AVVIO TEST: SCENARIO OVERLOAD ==="
	./$(BIN_DIR)/responsabile "config overload.conf"

.PHONY: all clean test_timeout test_overload