//------------------------------------------------------------------------------
// CLPKMCC
//
// Modified from Eli's tooling example
//------------------------------------------------------------------------------
#include <sstream>
#include <string>
#include <vector>

#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Analysis/CFG.h"
#include "clang/Analysis/CallGraph.h"
#include "clang/Frontend/ASTConsumers.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace clang::driver;
using namespace clang::tooling;

static llvm::cl::OptionCategory CLPKMCategory("CLPKM");

// By implementing RecursiveASTVisitor, we can specify which AST nodes
// we're interested in by overriding relevant methods.
class MyASTVisitor : public RecursiveASTVisitor<MyASTVisitor> {
public:
	MyASTVisitor(Rewriter& R) : TheRewriter(R) {

		}

	bool VisitStmt(Stmt* s) {

		// Only care about If statements.
		if (isa<IfStmt>(s)) {

			IfStmt* IfStatement = cast<IfStmt>(s);
			Stmt*   Then = IfStatement->getThen();
			Stmt*   Else = IfStatement->getElse();

			}

		return true;

		}

	bool VisitFunctionDecl(FunctionDecl *FuncDecl) {

		if (FuncDecl == nullptr)
			return false;

		DeclarationName DeclName = FuncDecl->getNameInfo().getName();
		std::string FuncName = DeclName.getAsString();

		if (!FuncDecl->hasAttr<OpenCLKernelAttr>()) {

			llvm::errs() << "Non-kernel function " << FuncName
			             << " should be inlined first.\n";
			return false;

			}

		size_t ParamSize = FuncDecl->param_size();
		const char* CLPKMParam = ", __global char * __clpkm_hdr, "
		                         "__global char * __clpkm_local, "
		                         "__global char * __clpkm_lvb";

		// FIXME: I can't figure out a graceful way to do this at the moment
		//        It's uncommon anyway
		if (ParamSize == 0)
			llvm_unreachable("function w/ parameter is yet implemented.");

		auto LastParam = FuncDecl->getParamDecl(ParamSize - 1);
		auto InsertCut = LastParam->getSourceRange().getEnd();

		TheRewriter.InsertTextAfterToken(InsertCut, CLPKMParam);

		if (!FuncDecl->hasBody())
			return true;

		CFG::BuildOptions Options;
		auto CFG = CFG::buildCFG(FuncDecl, FuncDecl->getBody(),
		                         &FuncDecl->getASTContext(), Options);

		llvm::errs() << "Dump CFG of funcion " << FuncName << '\n';
		CFG->dump(TheRewriter.getLangOpts(), true);

		return true;

		}

private:
	Rewriter &TheRewriter;

	};

// Implementation of the ASTConsumer interface for reading an AST produced
// by the Clang parser.
class MyASTConsumer : public ASTConsumer {
public:
	MyASTConsumer(Rewriter &R) : Visitor(R) {

		}

	bool HandleTopLevelDecl(DeclGroupRef DeclGroup) override {

		for (auto& Decl : DeclGroup) {

			if (!Decl->isInvalidDecl())
				CallGraph.addToCallGraph(Decl);

			}

		CallGraphNode* VirtualRoot = CallGraph.getRoot();

		llvm::errs() << "Dump virtual root\n";
		VirtualRoot->print(llvm::errs());
		llvm::errs() << '\n';

		return true;

		}


private:
	CallGraph CallGraph;
	MyASTVisitor Visitor;

	};



class CLPKMCCFrontendAction : public ASTFrontendAction {
public:
	CLPKMCCFrontendAction() = default;

	void EndSourceFileAction() override {

		SourceManager &SM = TheRewriter.getSourceMgr();
		TheRewriter.getEditBuffer(SM.getMainFileID()).write(llvm::outs());

		}

	std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
	                                               StringRef file) override {

		TheRewriter.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
		return llvm::make_unique<MyASTConsumer>(TheRewriter);

		}

	 bool BeginInvocation(CompilerInstance &CI) override {

		CI.getDiagnostics().setIgnoreAllWarnings(true);
		return true;
		
		}

private:
	Rewriter TheRewriter;

	};



int main(int ArgCount, const char* ArgVar[]) {

	CommonOptionsParser Options(ArgCount, ArgVar, CLPKMCategory);
	ClangTool Tool(Options.getCompilations(), Options.getSourcePathList());

	return Tool.run(newFrontendActionFactory<CLPKMCCFrontendAction>().get());

	}
