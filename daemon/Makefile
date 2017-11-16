CXX        := g++
PKG_CONFIG := pkg-config

CPPFLAGS = -D_FORTIFY_SOURCE=2
CXXFLAGS = $(shell $(PKG_CONFIG) --cflags libsystemd) \
           -Wall -Wextra -pedantic -fstack-protector-strong \
           -O2 -ftree-vectorize -fno-plt -flto -fPIE
LDFLAGS  = -Wl,-O1,--sort-common,--as-needed,-z,relro,-z,now -pie -s
LIBS     = $(shell $(PKG_CONFIG) --libs libsystemd) -lyaml-cpp

TARGET   := clpkm-daemon
SRCS     := $(wildcard *.cpp)
OBJS     := ${SRCS:%.cpp=%.o}

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) -std=c++17 $^ -o $@ $(LIBS)

%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -std=c++17 $< -c -o $@

.PHONY: clean

clean:
	$(RM) $(TARGET) *.o
