CXX         = clang++
LLVM_CONFIG = llvm-config

CXXFLAGS = -Wall -Wextra -pedantic -fstack-protector-strong -std=c++14 \
           # -O2 -ftree-vectorize -flto
LDFLAGS  = -Wl,-O1,--sort-common,--as-needed,-z,relro,-z,now # -s
LIBS     = 

CXXFLAGS += $(shell $(LLVM_CONFIG) --cxxflags)
LDFLAGS  += $(shell $(LLVM_CONFIG) --ldflags)
LIBS     += -Wl,--start-group $(shell $(LLVM_CONFIG) --libs) \
            -lclangAST -lclangAnalysis -lclangBasic -lclangCodeGen \
            -lclangDriver -lclangEdit -lclangFrontend -lclangFrontendTool \
            -lclangIndex -lclangLex -lclangParse -lclangRewrite -lclangSema \
            -lclangSerialization -lclangToolingCore -lclangTooling \
            -lclangFormat -lclangRewrite -lclangASTMatchers -Wl,--end-group

all: clpkmpp clpkmcc

clpkmpp:
	$(MAKE) -C pp TARGET=$@
	cp pp/$@ .

clpkmcc:
	$(MAKE) -C cc TARGET=$@
	cp cc/$@ .

.PHONY: clean

clean:
	$(RM) clpkmpp clpkmcc pp/*.o pp/clpkmpp cc/*.o cc/clpkmcc
