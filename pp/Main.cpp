//===----------------------------------------------------------------------===//
// CLPKMPP
//
// Modified from Eli's tooling example
//===----------------------------------------------------------------------===//
#include <algorithm>
#include <deque>
#include <string>
#include <unordered_map>
#include <utility>

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



class Extractor : public RecursiveASTVisitor<Extractor> {
public:
	Extractor(CompilerInstance& CI, Rewriter& R) :
		TheCI(CI), TheRewriter(R) {

		}

	bool VisitReturnStmt(ReturnStmt* RS) {

		if (RS == nullptr || RS->getRetValue() == nullptr)
			return true;

		// Workaround a mystery behaviour of Clang. The source range of the
		// statement below stops right before "b;" under some circumstances
		//   return a = b;
		TheRewriter.InsertTextBefore(RS->getRetValue()->getLocStart(), "(");
		TheRewriter.InsertTextAfterToken(RS->getRetValue()->getLocEnd(), ")");

		return true;

		}

	// The semicolor is not a part of a Expr. Append brace for CC.
	bool VisitIfStmt(IfStmt* IS) {

		if (IS == nullptr)
			return true;

		return AppendBrace(IS->getThen()) && AppendBrace(IS->getElse());

		}

	bool VisitWhileStmt(WhileStmt* WS) {

		if (WS == nullptr)
			return true;

		return AppendBrace(WS->getBody());

		}

	bool VisitForStmt(ForStmt* FS) {

		if (FS == nullptr)
			return true;

		return AppendBrace(FS->getBody());

		}

	bool VisitDoStmt(DoStmt* DS) {

		if (DS == nullptr)
			return true;

		return AppendBrace(DS->getBody());

		}

private:
	CompilerInstance& TheCI;
	Rewriter&         TheRewriter;

	bool AppendBrace(Stmt* S) {

		if (S == nullptr || !isa<Expr>(S))
			return true;

		auto LocEnd = Lexer::findLocationAfterToken(S->getLocEnd(),
		                                            tok::semi,
		                                            TheCI.getSourceManager(),
		                                            TheCI.getLangOpts(),
		                                            false);

		TheRewriter.InsertTextBefore(S->getLocStart(), " { ");
		TheRewriter.InsertTextAfterToken(LocEnd, " } ");

		return true;

		}

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

		SourceManager &SM = PPRewriter.getSourceMgr();
		PPRewriter.getEditBuffer(SM.getMainFileID()).write(llvm::outs());

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
