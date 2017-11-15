CXX = g++

CXXFLAGS = -Wall -Wextra -pedantic -fstack-protector-strong -g3 \
           -DCL_USE_DEPRECATED_OPENCL_1_2_APIS -Wno-ignored-attributes \
           # -O2 -ftree-vectorize -fno-plt -flto
LDFLAGS  = -Wl,-O1,--sort-common,--as-needed,-z,relro,-z,now # -s
LIBS     = $(shell $(PKG_CONFIG) --libs libsystemd) \
           -lyaml-cpp -lOpenCL -ldl -lboost_thread

TARGET := libclpkm.so
SRCS   := $(wildcard *.cpp)
OBJS   := ${SRCS:%.cpp=%.o}

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -std=c++17 -DHAVE_YAMLCPP -shared $^ -o $@ $(LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -std=c++17 -DHAVE_YAMLCPP -fPIC $< -c -o $@

.PHONY: clean

clean:
	$(RM) $(TARGET) *.o
