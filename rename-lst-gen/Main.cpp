/*
  Rename List Generator

  Modified from Eli's tooling example

*/

#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/ASTConsumers.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace clang::driver;
using namespace clang::tooling;

static llvm::cl::OptionCategory LstGenCategory("rename-lst-gen");



class LstGenVisitor : public RecursiveASTVisitor<LstGenVisitor> {
public:
	LstGenVisitor(llvm::raw_ostream& iOuts, const SourceManager& iSM)
	: Outs(iOuts), SM (iSM), Nonce(0) { }

	bool TraverseDecl(clang::Decl* D) {

		bool Ret = true;

		// Don't traverse headers
		if (D != nullptr && SM.isInMainFile(D->getLocation()))
			Ret = clang::RecursiveASTVisitor<LstGenVisitor>::TraverseDecl(D);

		return Ret;

		}

	bool VisitVarDecl(VarDecl* VD) {

		if (VD == nullptr)
			return true;

		const FileID MainFileID = SM.getMainFileID();

		auto ThisPos = VD->getLocation().getRawEncoding();
		auto StartPos = SM.getLocForStartOfFile(MainFileID).getRawEncoding();

		const char* VarName = VD->getIdentifier()->getNameStart();

		Outs << "-   Offset: " << (ThisPos - StartPos) << '\n'
		     << "    NewName: " << "__R_" << ++Nonce << '_' << VarName << '\n';

		return true;

		}

private:
	llvm::raw_ostream&   Outs;
	const SourceManager& SM;

	unsigned Nonce;

	};



class LstGenDriver : public ASTConsumer {
public:
	template <class ... P>
	LstGenDriver(P&& ... Param)
	: V(std::forward<P>(Param)...) { }

	bool HandleTopLevelDecl(DeclGroupRef DeclGroup) override {

		for (auto& Decl : DeclGroup)
			V.TraverseDecl(Decl);

		return true;

		}

private:
	LstGenVisitor V;

	};



class RenameLstGenFA : public ASTFrontendAction {
public:
	RenameLstGenFA() = default;

	std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
	                                               StringRef file) override {

		return std::make_unique<LstGenDriver>(
				llvm::outs(), CI.getSourceManager());

		}

	 bool BeginInvocation(CompilerInstance &CI) override {

		CI.getDiagnostics().setIgnoreAllWarnings(true);
		return true;

		}

private:

	};



int main(int ArgCount, const char* ArgVar[]) {

	CommonOptionsParser Options(ArgCount, ArgVar, LstGenCategory);
	ClangTool Tool(Options.getCompilations(), Options.getSourcePathList());

	return Tool.run(newFrontendActionFactory<RenameLstGenFA>().get());

	}
