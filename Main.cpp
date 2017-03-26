//------------------------------------------------------------------------------
// Tooling sample. Demonstrates:
//
// * How to write a simple source tool using libTooling.
// * How to use RecursiveASTVisitor to find interesting AST nodes.
// * How to use the Rewriter API to rewrite the source code.
//
// Eli Bendersky (eliben@gmail.com)
// This code is in the public domain
//------------------------------------------------------------------------------
#include <sstream>
#include <string>
#include <vector>

#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
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

		TheRewriter.InsertText(Then->getLocStart(),
		                       "// the 'if' part\n",
		                       true, true);

		if (Else)
			TheRewriter.InsertText(Else->getLocStart(),
			                       "// the 'else' part\n",
			                       true, true);

		}

	return true;

	}

	bool VisitFunctionDecl(FunctionDecl *f) {

		if (f == nullptr)
			return false;

		DeclarationName DeclName = f->getNameInfo().getName();
		std::string FuncName = DeclName.getAsString();

		size_t ParamSize = f->param_size();
		std::string CLPKMParam = "const __global char * __clpkm_hdr, "
		                         "const __global char * __clpkm_local, "
		                         "const __global char * __clpkm_lvb";

		// FIXME: I can't figure out a graceful way to do this at the moment
		//        It's uncommon case anyway
		if (ParamSize == 0)
			llvm_unreachable("function w/ parameter is yet implemented.");
		else {

			auto LastParam = f->getParamDecl(ParamSize - 1);
			auto InsertCut = LastParam->getSourceRange().getEnd();

			CLPKMParam = ", " + CLPKMParam;
			TheRewriter.InsertTextAfterToken(InsertCut, CLPKMParam);

			}

		if (!f->hasBody()) {

			llvm::errs() << "Warning: skipping declaration of " << FuncName << "\n";
			return true;

			}

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

	// Override the method that gets called for each parsed top-level
	// declaration.
	bool HandleTopLevelDecl(DeclGroupRef DR) override {

		for (auto& b : DR) {

			if (b->isInvalidDecl() || !Visitor.TraverseDecl(b))
				return false;

			b->dump();
		
			}

		return true;

		}

private:
	MyASTVisitor Visitor;

	};



// For each source file provided to the tool, a new FrontendAction is created.
class MyFrontendAction : public ASTFrontendAction {
public:
	MyFrontendAction() {}

	void EndSourceFileAction() override {

		SourceManager &SM = TheRewriter.getSourceMgr();

		llvm::errs() << "** EndSourceFileAction for: "
		             << SM.getFileEntryForID(SM.getMainFileID())->getName()
		             << "\n";

		TheRewriter.getEditBuffer(SM.getMainFileID()).write(llvm::outs());

		}

	std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
	                                               StringRef file) override {

		llvm::errs() << "** Creating AST consumer for: " << file << "\n";
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



int main(int argc, const char **argv) {

	CommonOptionsParser op(argc, argv, CLPKMCategory);
	ClangTool Tool(op.getCompilations(), op.getSourcePathList());

	Tool.run(newFrontendActionFactory<MyFrontendAction>().get());
	Tool.run(newFrontendActionFactory<MyFrontendAction>().get());


	}
