//===----------------------------------------------------------------------===//
// CLPKMPP
//
// Modified from Eli's tooling example
//===----------------------------------------------------------------------===//
#include <algorithm>
#include <deque>
#include <string>
#include <unordered_set>
#include <utility>

#include "clang/Analysis/Analyses/LiveVariables.h"
#include "clang/Analysis/CFGStmtMap.h"
#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Frontend/ASTConsumers.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Lex/Lexer.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "clpkmpp"

using namespace clang;
using namespace clang::driver;
using namespace clang::tooling;

static llvm::cl::OptionCategory CLPKMPPCategory("CLPKMPP");


class LiveValueIterator : public std::unordered_set<VarDecl*>::iterator {
	};

bool IsLive(VarDecl* VD, Stmt* S, LiveVariables* LV, CFGStmtMap* M) {

	if (VD == nullptr || S == nullptr || LV == nullptr || M == nullptr)
		return false;

	CFGBlock* B = M->getBlock(S);

	if (B == nullptr)
		return false;

	return LV->isLive(B, VD);

	}


class Extractor : public RecursiveASTVisitor<Extractor> {
public:
	Extractor(CompilerInstance& CI, Rewriter& R) :
		TheCI(CI), TheRewriter(R) {

		}

	clang::AnalysisDeclContextManager* ADCM;
	clang::AnalysisDeclContext* func_ADC;
	CFGStmtMap* CFGMap;
	clang::LiveVariables *LV;
	std::unordered_set<VarDecl*> DeclSet;

	bool TraverseFunctionDecl(FunctionDecl* FD) {

		if (FD == nullptr || !FD->hasBody())
			return true;

		std::string FuncName = FD->getNameInfo().getName().getAsString();

		llvm::errs() << "\n====================\n"
		             << FuncName
		             << "\n====================\n";

		ADCM = new clang::AnalysisDeclContextManager;
		func_ADC = ADCM->getContext(FD);
		// Or it won't add sub-expr
		func_ADC->getCFGBuildOptions().setAllAlwaysAdd();

		LV = clang::LiveVariables::computeLiveness(*func_ADC, false);
		CFGMap = func_ADC->getCFGStmtMap();

//		func_LV->runOnAllBlocks(*obs);
		LV->dumpBlockLiveness((func_ADC->getASTContext()).getSourceManager());

		llvm::errs() << "\n====================\n"
		             << FuncName << " CFG"
		             << "\n====================\n";
		func_ADC->getCFG()->dump(TheCI.getLangOpts(), true);

		RecursiveASTVisitor<Extractor>::TraverseFunctionDecl(FD);

		CFGMap = nullptr;
		DeclSet.clear();
		delete LV;
		LV = nullptr;
//		delete func_ADC;
//		func_ADC = nullptr;
		delete ADCM;
		ADCM = nullptr;

		return true;

		}

	bool VisitVarDecl(VarDecl* VD) {

		if (VD != nullptr)
			DeclSet.emplace(VD);

		return true;

		}

	bool VisitReturnStmt(ReturnStmt* RS) {

		if (RS == nullptr)
			return true;

		auto PrintIfLive = [&](VarDecl* VD) -> void {
			if (IsLive(VD, RS, LV, CFGMap))
				llvm::errs() << TheRewriter.getRewrittenText(VD->getSourceRange()) << '\n';
			};

		llvm::errs() << "\n====================\n"
		                "LVs in ReturnStmt\n"
		                "--------------------\n";
		std::for_each(DeclSet.begin(), DeclSet.end(), PrintIfLive);
		llvm::errs() << "\n====================\n";

		return true;

		}

	bool VisitDoStmt(DoStmt* DS) {

		if (DS == nullptr)
			return true;

		assert(LV != nullptr && "LV absent");
		assert(DS->getBody() != nullptr && "getBody returns nullptr");

		auto PrintIfLive = [&](VarDecl* VD) -> void {
			if (IsLive(VD, DS, LV, CFGMap))
				llvm::errs() << TheRewriter.getRewrittenText(VD->getSourceRange()) << '\n';
			};

		llvm::errs() << "\n====================\n"
		                "LVs in DoStmt\n"
		                "--------------------\n";
		std::for_each(DeclSet.begin(), DeclSet.end(), PrintIfLive);
		llvm::errs() << "\n====================\n";

		return true;

		}

#if 0
	bool VisitStmt(Stmt* S) {

		if (S == nullptr)
			return true;
#define START_CODE /*"\x1b[4m"*/ "\x1b[3m"
#define END_CODE /*"\x1b[24m"*/ "\x1b[0m"
		llvm::errs() << "Stmt: " START_CODE
		             << TheRewriter.getRewrittenText({S->getLocStart(),
		                                             S->getLocEnd()})
		             << END_CODE "\n";

		if (!S->getStmtLocEnd().isValid())
			return true;

		llvm::errs() << "--> Stmt(patched): " START_CODE
		             << TheRewriter.getRewrittenText({S->getLocStart(),
		                                              S->getStmtLocEnd()})
		             << END_CODE "\n";

		return true;

		}

	bool VisitExpr(Expr* E) {

		if (E == nullptr)
			return true;

		llvm::errs() << "Expr: " START_CODE
		             << TheRewriter.getRewrittenText({E->getLocStart(),
		                                              E->getLocEnd()})
		             << END_CODE "\n";

		if (!E->getSemiLoc().isValid())
			return true;

		llvm::errs() << "--> Expr(patched): " START_CODE
		             << TheRewriter.getRewrittenText({E->getLocStart(),
		                                              E->getSemiLoc()})
		             << END_CODE "\n";

		return true;

		}
#endif
private:
	CompilerInstance& TheCI;
	Rewriter&         TheRewriter;

	};



class ExtractorDriver : public ASTConsumer {
public:
	template <class ... P>
	ExtractorDriver(P&& ... Param) :
		Visitor(std::forward<P>(Param)...) {

		}

	bool HandleTopLevelDecl(DeclGroupRef DeclGroup) override {

		for (auto& Decl : DeclGroup)
			Visitor.TraverseDecl(Decl);

		return true;

		}

private:
	Extractor Visitor;

	};



class CLPKMPPFrontendAction : public ASTFrontendAction {
public:
	CLPKMPPFrontendAction() = default;

	void EndSourceFileAction() override {

		if (getCompilerInstance().getDiagnostics().hasErrorOccurred())
			return;

//		SourceManager &SM = PPRewriter.getSourceMgr();
//		PPRewriter.getEditBuffer(SM.getMainFileID()).write(llvm::outs());

		}

	std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
	                                               StringRef file) override {

		PPRewriter.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
		return llvm::make_unique<ExtractorDriver>(CI, PPRewriter);

		}

	 bool BeginInvocation(CompilerInstance &CI) override {

		CI.getDiagnostics().setIgnoreAllWarnings(true);
		return true;
		
		}

private:
	Rewriter PPRewriter;

	};



int main(int ArgCount, const char* ArgVar[]) {

	CommonOptionsParser Options(ArgCount, ArgVar, CLPKMPPCategory);
	ClangTool Tool(Options.getCompilations(), Options.getSourcePathList());

	return Tool.run(newFrontendActionFactory<CLPKMPPFrontendAction>().get());

	}
