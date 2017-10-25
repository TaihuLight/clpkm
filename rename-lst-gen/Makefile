CXX         := clang++
LLVM_CONFIG := llvm-config

CXXFLAGS := $(shell $(LLVM_CONFIG) --cxxflags)
LDFLAGS  := $(shell $(LLVM_CONFIG) --ldflags)
LIBS     := -Wl,--start-group $(shell $(LLVM_CONFIG) --libs) \
            -lclangAST -lclangAnalysis -lclangASTMatchers -lclangBasic \
            -lclangCodeGen -lclangDriver -lclangEdit -lclangFormat \
            -lclangFrontend -lclangFrontendTool -lclangIndex -lclangLex \
            -lclangParse -lclangRewrite -lclangSema -lclangSerialization \
            -lclangTooling -lclangToolingCore -Wl,--end-group

TARGET   := rename-lst-gen
SRCS     := $(wildcard *.cpp)
OBJS     := ${SRCS:%.cpp=%.o}

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -std=c++17 $^ -o $@ $(LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -std=c++17 $< -c -o $@

.PHONY: clean

clean:
	$(RM) $(TARGET) *.o
