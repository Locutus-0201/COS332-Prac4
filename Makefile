# Compiler settings
CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++11 -O2

# Target executable name
TARGET = server

# Source files
SRCS = server.cpp

# Object files
OBJS = $(SRCS:.cpp=.o)

# Deployment directory
DEPLOY_DIR = deploy

# Default rule: build the server
all: $(TARGET)

# Linking the object files
$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS)

# Compiling the source file
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# ==========================================
# NEW: DEPLOYMENT COMMANDS
# ==========================================

# Creates the deploy folder and copies the executable there
deploy: all
	@echo "==> Deploying to $(DEPLOY_DIR)/..."
	mkdir -p $(DEPLOY_DIR)
	cp $(TARGET) $(DEPLOY_DIR)/
	@echo "==> Deployment complete!"

# Compiles, deploys, and instantly runs the server from the deploy folder
run: deploy
	@echo "==> Starting server in $(DEPLOY_DIR)/..."
	cd $(DEPLOY_DIR) && ./$(TARGET)

# Cleans up the compiled objects AND the deploy folder
clean:
	@echo "==> Cleaning workspace..."
	rm -f $(OBJS) $(TARGET)
	rm -rf $(DEPLOY_DIR)