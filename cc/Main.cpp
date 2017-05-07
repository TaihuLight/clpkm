//===----------------------------------------------------------------------===//
// CLPKMCC
//
// Modified from Eli's tooling example
//===----------------------------------------------------------------------===//
#include <string>
#include <utility>

#include "clang/Analysis/CFG.h"
#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Frontend/ASTConsumers.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "clpkmcc"

using namespace clang;
using namespace clang::driver;
using namespace clang::tooling;

static llvm::cl::OptionCategory CLPKMCCCategory("CLPKMCC");



class Extractor : public RecursiveASTVisitor<Extractor> {
public:
	Extractor(Rewriter& R, DiagnosticsEngine& DE) :
		TheRewriter(R), TheDiag(DE) {
		;
		}

	bool VisitSwitchStmt(SwitchStmt* SS) {

		auto DiagID = TheDiag.getCustomDiagID(DiagnosticsEngine::Level::Error, "switch is not supported");
		TheDiag.Report(SS->getLocStart(), DiagID);

		return false;

		}

	// Patch barrier
	bool VisitCallExpr(CallExpr* CE) {

		if (CE->getDirectCallee()->getNameInfo().getName().getAsString() != "barrier")
			return true;

		// TODO

		return true;

		}

	// 1.   Patch arguments
	// 2.   Re-arrange BBs
	// 3.   Insert Checkpoint/Resume codes
	bool TraverseFunctionDecl(FunctionDecl* FuncDecl) {

		if (FuncDecl == nullptr)
			return false;

		if (!FuncDecl->hasAttr<OpenCLKernelAttr>())
			return true;

		// FIXME: I can't figure out a graceful way to do this at the moment
		//        It's uncommon anyway
		if (auto ParamSize = FuncDecl->param_size(); ParamSize <= 0) {

			auto DiagID = TheDiag.getCustomDiagID(DiagnosticsEngine::Level::Warning, "skipping nullary function");
			TheDiag.Report(FuncDecl->getNameInfo().getLoc(), DiagID);

			// TODO: remove body of nullary function?
			return true;

			}
		// Append arguments
		else {

			auto LastParam = FuncDecl->getParamDecl(ParamSize - 1);
			auto InsertCut = LastParam->getSourceRange().getEnd();
			const char* CLPKMParam = ", __global char * __clpkm_hdr, "
			                         "__global char * __clpkm_local, "
			                         "__global char * __clpkm_prv";

			TheRewriter.InsertTextAfterToken(InsertCut, CLPKMParam);

			}

		if (!FuncDecl->hasBody())
			return true;

		RecursiveASTVisitor<Extractor>::TraverseFunctionDecl(FuncDecl);

		// TODO
		std::string FuncName = FuncDecl->getNameInfo().getName().getAsString();

		return true;

		}

private:
	Rewriter&          TheRewriter;
	DiagnosticsEngine& TheDiag;

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



class CLPKMCCFrontendAction : public ASTFrontendAction {
public:
	CLPKMCCFrontendAction() = default;

	void EndSourceFileAction() override {

		if (getCompilerInstance().getDiagnostics().hasErrorOccurred())
			return;

		SourceManager &SM = CCRewriter.getSourceMgr();
		CCRewriter.getEditBuffer(SM.getMainFileID()).write(llvm::outs());

		}

	std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
	                                               StringRef file) override {

		CCRewriter.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
		return llvm::make_unique<ExtractorDriver>(CCRewriter, getCompilerInstance().getDiagnostics());

		}

	 bool BeginInvocation(CompilerInstance &CI) override {

		CI.getDiagnostics().setIgnoreAllWarnings(true);
		return true;
		
		}

private:
	Rewriter CCRewriter;

	};



int main(int ArgCount, const char* ArgVar[]) {

	CommonOptionsParser Options(ArgCount, ArgVar, CLPKMCCCategory);
	ClangTool Tool(Options.getCompilations(), Options.getSourcePathList());

	return Tool.run(newFrontendActionFactory<CLPKMCCFrontendAction>().get());

	}
