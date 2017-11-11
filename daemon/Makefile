CXX        := g++
PKG_CONFIG := pkg-config

CPPFLAGS = -D_FORTIFY_SOURCE=2
CXXFLAGS = $(shell $(PKG_CONFIG) --cflags libsystemd) \
           -Wall -Wextra -pedantic -fstack-protector-strong -g3 \
           # -O2 -ftree-vectorize -fno-plt -flto
LDFLAGS  = -Wl,-O1,--sort-common,--as-needed,-z,relro,-z,now # -s
LIBS     = $(shell $(PKG_CONFIG) --libs libsystemd)

TARGET   := clpkm-daemon
SRCS     := $(wildcard *.cpp)
OBJS     := ${SRCS:%.cpp=%.o}

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -pie -std=c++17 $^ -o $@ $(LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -fPIE -std=c++17 $< -c -o $@

.PHONY: clean

clean:
	$(RM) $(TARGET) *.o
