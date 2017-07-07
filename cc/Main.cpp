//===----------------------------------------------------------------------===//
// CLPKMCC
//
// Modified from Eli's tooling example
//===----------------------------------------------------------------------===//

#include "Instrumentor.hpp"

#include "clang/AST/ASTConsumer.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Frontend/ASTConsumers.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"

#include <utility>

#define DEBUG_TYPE "clpkmcc"

using namespace clang;
using namespace clang::driver;
using namespace clang::tooling;

static llvm::cl::OptionCategory CLPKMCCCat("CLPKMCC");
static llvm::cl::opt<std::string> OptSourceOut(
	"source-output", llvm::cl::desc("Specify the source output filename"),
	llvm::cl::value_desc("filename"), llvm::cl::cat(CLPKMCCCat));
static llvm::cl::opt<std::string> OptProfileOut(
	"profile-output", llvm::cl::desc("Specify the profile output filename"),
	llvm::cl::value_desc("filename"), llvm::cl::cat(CLPKMCCCat));



// Helper class only for Main.cpp
class OutputHelper {
public:
	OutputHelper()
	: SOut(nullptr), YOut(nullptr) { }

	~OutputHelper() { if (YOut != nullptr) *YOut << PL; }

	std::string Initialize() {
		std::error_code EC;

		// Init source output
		// If not specified, default to llvm::outs()
		if (OptSourceOut.empty())
			SOut = &llvm::outs();
		else {
			__SOut = llvm::make_unique<llvm::raw_fd_ostream>(
				OptSourceOut, EC, llvm::sys::fs::F_Text);
			SOut = __SOut.get();
			}

		if (EC)
			return "Failed to open source output: " + EC.message();

		// Init profile output
		// If not specified, don't init
		if (OptProfileOut.empty())
			return std::string();
		else
			__POut = llvm::make_unique<llvm::raw_fd_ostream>(
				OptProfileOut, EC, llvm::sys::fs::F_Text);

		if (EC)
			return "Failed to open profile output: " + EC.message();

		__YOut = llvm::make_unique<llvm::yaml::Output>(*__POut.get());
		YOut = __YOut.get();
		return std::string();

		}

	llvm::raw_ostream& getSourceOutput() { return *SOut; }
	llvm::yaml::Output& getProfileOutput() { return *YOut; }
	ProfileList& getProfileList() { return PL; }

private:
	llvm::raw_ostream*  SOut;
	llvm::yaml::Output* YOut;

	// Destruct in reverse order
	std::unique_ptr<llvm::raw_fd_ostream> __SOut;
	std::unique_ptr<llvm::raw_fd_ostream> __POut;
	std::unique_ptr<llvm::yaml::Output>   __YOut;

	ProfileList PL;

	};



class Driver : public ASTConsumer {
public:
	template <class ... P>
	Driver(P&& ... Param)
	: Visitor(std::forward<P>(Param)...) { }

	bool HandleTopLevelDecl(DeclGroupRef DeclGroup) override {

		for (auto& Decl : DeclGroup)
			Visitor.TraverseDecl(Decl);

		return true;

		}

private:
	Instrumentor Visitor;

	};



class CLPKMCCFA : public ASTFrontendAction {
public:
	CLPKMCCFA() = default;

	void EndSourceFileAction() override {

		if (getCompilerInstance().getDiagnostics().hasErrorOccurred())
			return;

		SourceManager &SM = CCRewriter.getSourceMgr();
		CCRewriter.getEditBuffer(SM.getMainFileID()).write(Helper.getSourceOutput());

		}

	std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
	                                               StringRef file) override {

		CCRewriter.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
		return llvm::make_unique<Driver>(CCRewriter, CI, Helper.getProfileList());

		}

	 bool BeginInvocation(CompilerInstance &CI) override {

		// Suppress warnings not produced by CLPKMCC
		CI.getDiagnostics().setIgnoreAllWarnings(true);
		static bool Inited = false, Failed = false;

		if (!Inited) {
			if (std::string ErrMsg = Helper.Initialize(); !ErrMsg.empty()) {
				Failed = true;
				DiagReport(DiagnosticsEngine::Level::Error, "%0") << ErrMsg;
				}
			Inited = true;
			}

		return !Failed;

		}

private:
	template <unsigned N>
	DiagnosticBuilder DiagReport(DiagnosticsEngine::Level LV,
	                             const char (&FormatStr)[N]) {
		auto& Diag = getCompilerInstance().getDiagnostics();
		auto  DiagID = Diag.getCustomDiagID(LV, FormatStr);
		return Diag.Report(SourceLocation(), DiagID);
		}

	Rewriter CCRewriter;
	static OutputHelper Helper;

	};

OutputHelper CLPKMCCFA::Helper;



int main(int ArgCount, const char* ArgVar[]) {

	CommonOptionsParser Options(ArgCount, ArgVar, CLPKMCCCat);
	ClangTool Tool(Options.getCompilations(), Options.getSourcePathList());

	return Tool.run(newFrontendActionFactory<CLPKMCCFA>().get());

	}
