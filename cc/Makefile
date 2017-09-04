CXX         := clang++
LLVM_CONFIG := llvm-config

CXXFLAGS := -Wall -Wextra $(shell $(LLVM_CONFIG) --cxxflags)
LDFLAGS  := $(shell $(LLVM_CONFIG) --ldflags)
LIBS     := -Wl,--start-group $(shell $(LLVM_CONFIG) --libs) \
            -lclangAST -lclangAnalysis -lclangBasic -lclangCodeGen \
            -lclangDriver -lclangEdit -lclangFrontend -lclangFrontendTool \
            -lclangIndex -lclangLex -lclangParse -lclangRewrite -lclangSema \
            -lclangSerialization -lclangToolingCore -lclangTooling \
            -lclangFormat -lclangRewrite -lclangASTMatchers -Wl,--end-group

TARGET   := clpkmcc
SRCS     := $(wildcard *.cpp)
OBJS     := ${SRCS:%.cpp=%.o}

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -std=c++17 -DHAVE_LLVM $^ -o $@ $(LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -std=c++17 -DHAVE_LLVM $< -c -o $@

.PHONY: clean

clean:
	$(RM) $(TARGET) *.o
