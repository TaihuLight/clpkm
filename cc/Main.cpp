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
#include "clang/Lex/Lexer.h"
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
	Extractor(Rewriter& R, CompilerInstance& CI) :
		TheRewriter(R), TheCI(CI) {
		;
		}

	bool VisitSwitchStmt(SwitchStmt* SS) {

		auto& TheDiag = TheCI.getDiagnostics();
		auto  DiagID = TheDiag.getCustomDiagID(DiagnosticsEngine::Level::Error,
		                                       "switch is not supported");
		TheDiag.Report(SS->getLocStart(), DiagID);

		return false;

		}

	bool VisitStmt(Stmt* S) {

		CostCounter++;
		return true;

		}

	// Patch barrier
	bool VisitCallExpr(CallExpr* CE) {

		if (CE == nullptr || CE->getDirectCallee() == nullptr)
			return true;

		if (CE->getDirectCallee()->getNameInfo().getName().getAsString() !=
		    "barrier")
			return true;

		// TODO

		return true;

		}

	bool TraverseForStmt(ForStmt* FS) {

		if (FS == nullptr)
			return true;

		size_t OldCost = CostCounter;
		RecursiveASTVisitor<Extractor>::TraverseForStmt(FS);

		return PatchLoopBody(OldCost, CostCounter, FS, FS->getBody());

		}

	bool TraverseDoStmt(DoStmt* DS) {

		if (DS == nullptr)
			return true;

		size_t OldCost = CostCounter;
		RecursiveASTVisitor<Extractor>::TraverseDoStmt(DS);

		return PatchLoopBody(OldCost, CostCounter, DS, DS->getBody());

		}

	bool TraverseWhileStmt(WhileStmt* WS) {

		if (WS == nullptr)
			return true;

		size_t OldCost = CostCounter;
		RecursiveASTVisitor<Extractor>::TraverseWhileStmt(WS);

		return PatchLoopBody(OldCost, CostCounter, WS, WS->getBody());

		}

	// 1.   Patch arguments
	// 2.   Re-arrange BBs
	// 3.   Insert Checkpoint/Resume codes
	bool TraverseFunctionDecl(FunctionDecl* FuncDecl) {

		if (FuncDecl == nullptr)
			return true;

		if (!FuncDecl->hasAttr<OpenCLKernelAttr>())
			return true;

		// FIXME: I can't figure out a graceful way to do this at the moment
		//        It's uncommon anyway
		if (auto ParamSize = FuncDecl->param_size(); ParamSize <= 0) {

			auto& TheDiag = TheCI.getDiagnostics();
			auto DiagID = TheDiag.getCustomDiagID(DiagnosticsEngine::Level::Warning, "skipping nullary function");
			TheDiag.Report(FuncDecl->getNameInfo().getLoc(), DiagID);

			// TODO: remove body of nullary function?
			return true;

			}
		// Append arguments
		else {

			auto LastParam = FuncDecl->getParamDecl(ParamSize - 1);
			auto InsertCut = LastParam->getSourceRange().getEnd();
			const char* CLPKMParam = ", __global int * __clpkm_hdr, "
			                         "__global char * __clpkm_local, "
			                         "__global char * __clpkm_prv, "
			                         "__const size_t __clpkm_tlv";

			TheRewriter.InsertTextAfterToken(InsertCut, CLPKMParam);

			}

		if (!FuncDecl->hasBody())
			return true;

		CostCounter = 0;
		Nonce = 1;

		std::string FuncName = FuncDecl->getNameInfo().getName().getAsString();

		TheRewriter.InsertTextAfterToken(
			FuncDecl->getBody()->getLocStart(),
			"\n  size_t __clpkm_id = get_global_linear_id();\n"
			"  size_t __clpkm_ctr = 0;\n"
			"  switch (__clpkm_hdr[__clpkm_id]) {\n"
			"  default:  return;\n"
			"  case 1: ;\n");

		TheRewriter.InsertTextBefore(FuncDecl->getBody()->getLocEnd(),
		                             "\n  }\n  __clpkm_hdr[__clpkm_id] = 0;\n");

		return RecursiveASTVisitor<Extractor>::TraverseFunctionDecl(FuncDecl);

		}

private:
	Rewriter&         TheRewriter;
	CompilerInstance& TheCI;

	size_t CostCounter;
	size_t Nonce;

	bool PatchLoopBody(size_t OldCost, size_t NewCost, Stmt* Loop, Stmt* Body) {

		CostCounter = OldCost;

		std::string ThisNonce = std::to_string(++Nonce);
		std::string InstCR = " __clpkm_ctr += " +
		                     std::to_string(NewCost - OldCost) + "; case " +
		                     ThisNonce + ": "
		                     "if (__clpkm_ctr > __clpkm_tlv) {"
		                     "  __clpkm_hdr[__clpkm_id] = " + ThisNonce +
		                     "; /* C */ return; } "
		                     "else if (__clpkm_ctr <= 0) { /* R */ } ";

		if (isa<CompoundStmt>(Body))
			TheRewriter.InsertTextBefore(Body->getLocEnd(), InstCR);
		else {

			TheRewriter.InsertTextBefore(Body->getLocStart(), " { ");
			TheRewriter.InsertTextAfterToken(Body->getLocEnd(), InstCR += " } ");

			}

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
		return llvm::make_unique<ExtractorDriver>(CCRewriter, CI);

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
