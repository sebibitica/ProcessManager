# Makefile for process_manager

# Compiler
CXX = g++

# Compiler flags
CXXFLAGS = -std=c++17

# Linker flags (for ncurses)
LDFLAGS = -lncurses

# Source files
SRCS = process_manager.cpp

# Executable name
TARGET = process_manager

# Default target
all: $(TARGET)

# Rule to build the executable
$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

# Rule to install dependencies
install-dependencies:
	sudo apt-get update
	sudo apt-get install -y g++ libncurses5-dev libncursesw5-dev

# Rule to clean the project
clean:
	rm -f $(TARGET)