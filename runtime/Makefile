CXX         = clang++
LLVM_CONFIG = llvm-config

CXXFLAGS = -Wall -Wextra -pedantic -fstack-protector-strong \
           # -O2 -ftree-vectorize -flto
LDFLAGS  = -Wl,-O1,--sort-common,--as-needed,-z,relro,-z,now # -s
LIBS     = -lyaml-cpp -lOpenCL -ldl

TARGET   := libclpkm.so
SRCS     := $(wildcard *.cpp)
OBJS     := ${SRCS:%.cpp=%.o}

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -std=c++14 -DHAVE_YAMLCPP -fPIE -pie $^ -o $@ $(LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -std=c++14 -DHAVE_YAMLCPP -fPIE $< -c -o $@

.PHONY: clean

clean:
	$(RM) $(TARGET) *.o
