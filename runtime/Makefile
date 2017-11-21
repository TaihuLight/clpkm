CXX        := g++
PKG_CONFIG := pkg-config

CPPFLAGS = -D_FORTIFY_SOURCE=2
CXXFLAGS = $(shell $(PKG_CONFIG) --cflags libsystemd) \
           -Wall -Wextra -pedantic -fstack-protector-strong -g3 -Og \
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
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) \
	-std=c++17 -pthread -DHAVE_YAMLCPP -fPIC -shared $^ -o $@ $(LIBS)

%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) \
	-std=c++17 -pthread -DHAVE_YAMLCPP -fPIC $< -c -o $@

.PHONY: clean

clean:
	$(RM) $(TARGET) *.o
