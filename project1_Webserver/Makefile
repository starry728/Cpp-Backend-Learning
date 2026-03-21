# Makefile - C++11 多Reactor Web服务器

# 编译器设置
CXX = g++
CXXFLAGS = -std=c++11 -O2 -Wall -Wextra -pthread \
           -I./src \
           -I./src/server \
           -I./src/pool \
           -I./src/service

# 链接库
LDFLAGS = -pthread -lmysqlclient -ljsoncpp

# 目标可执行文件
TARGET = webserver

# 源文件目录
SRCDIR = src
SERVERDIR = $(SRCDIR)/server
POOLDIR = $(SRCDIR)/pool
SERVICEDIR = $(SRCDIR)/service

# 所有源文件
SOURCES = $(SRCDIR)/main.cpp \
          $(SERVERDIR)/Server.cpp \
          $(SERVERDIR)/SubReactor.cpp \
          $(SERVERDIR)/HttpRequest.cpp \
          $(POOLDIR)/ThreadPool.cpp \
          $(POOLDIR)/ConnectionPool.cpp \
          $(POOLDIR)/MysqlConn.cpp \
          $(SERVICEDIR)/Router.cpp \
          $(SERVICEDIR)/SessionManager.cpp \
          $(SERVICEDIR)/AuthService.cpp \
          $(SERVICEDIR)/FileService.cpp

# 对象文件
OBJECTS = $(SOURCES:.cpp=.o)

# 头文件依赖（用于增量编译）
DEPS = $(SOURCES:.cpp=.d)

# 默认目标
all: $(TARGET)

# 链接可执行文件
$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) -o $@ $(LDFLAGS)
	@echo "编译完成: $@"

# 编译源文件为对象文件，同时生成依赖关系
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

# 包含自动生成的依赖关系
-include $(DEPS)

# 清理编译产物
clean:
	rm -f $(OBJECTS) $(DEPS) $(TARGET)
	@echo "清理完成"

# 安装（复制到系统目录，可选）
install: $(TARGET)
	cp $(TARGET) /usr/local/bin/
	cp -r www /var/webserver/
	@echo "安装完成"

# 运行测试
run: $(TARGET)
	./$(TARGET) 8080 ./www 4

# 调试模式编译
debug: CXXFLAGS = -std=c++11 -g -O0 -Wall -pthread \
                 -I./src -I./src/server -I./src/pool -I./src/service \
                 -DDEBUG  -g -save-temps  # 添加调试和保存中间文件
debug: clean $(TARGET)

# 静态分析检查（需要cppcheck）
check:
	cppcheck --enable=all --std=c++11 --suppress=missingIncludeSystem $(SRCDIR)

.PHONY: all clean install run debug check

# 检查头文件依赖
check-headers:
	@echo "Checking HttpRequest.h..."
	@grep -n "getCookie\|getParam" src/server/HttpRequest.h || echo "Methods not found in header"
	@echo "Checking include paths..."
	@find . -name "*.cpp" -exec grep -l "HttpRequest" {} \;
	@echo "Header check complete"