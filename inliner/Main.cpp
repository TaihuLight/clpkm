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
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "inliner"

using namespace clang;
using namespace clang::driver;
using namespace clang::tooling;

static llvm::cl::OptionCategory InlinerCategory("Inliner");



using CallSiteTable = std::unordered_map<FunctionDecl*, std::deque<CallExpr*>>;
using RetStmtTable = std::unordered_map<FunctionDecl*, std::deque<ReturnStmt*>>;
using CodeSnippet = std::deque<std::string>;
using CodeCache = std::unordered_map<FunctionDecl*, CodeSnippet>;



class Extractor : public RecursiveASTVisitor<Extractor> {
public:
	Extractor(CallSiteTable& CST, RetStmtTable& RST, CompilerInstance& CI) :
		TheCST(CST), TheRST(RST), TheCI(CI), TheFunction(nullptr) {

		}

	bool VisitGotoStmt(GotoStmt* GS) {

		auto& TheDiag = TheCI.getDiagnostics();
		auto  DID = TheDiag.getCustomDiagID(DiagnosticsEngine::Level::Error,
		                                    "goto is not supported");
		TheDiag.Report(GS->getLocStart(), DID);

		return false;

		}

	bool TraverseFunctionDecl(FunctionDecl* FuncDecl) {

		TheFunction = FuncDecl;
		RecursiveASTVisitor<Extractor>::TraverseFunctionDecl(FuncDecl);
		TheFunction = nullptr;

		return true;

		}

	bool VisitReturnStmt(ReturnStmt* RS) {

		if (RS == nullptr)
			return true;

		assert(TheFunction != nullptr && "ReturnStmt outside of function?");
		TheRST[TheFunction].emplace_back(RS);
		return true;

		}

	bool VisitCallExpr(CallExpr* CE) {

		if (CE == nullptr)
			return true;

		assert(TheFunction != nullptr && "CallExpr outside of function?");
		TheCST[TheFunction].emplace_back(CE);
		return true;

		}

private:
	CallSiteTable&    TheCST;
	RetStmtTable&     TheRST;
	CompilerInstance& TheCI;

	FunctionDecl*  TheFunction;

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



class InlinerFrontendAction : public ASTFrontendAction {
public:
	InlinerFrontendAction() = default;

	void EndSourceFileAction() override {

		if (getCompilerInstance().getDiagnostics().hasErrorOccurred())
			return;

		SourceManager &SM = InlinerRewriter.getSourceMgr();
		InlinerRewriter.getEditBuffer(SM.getMainFileID()).write(llvm::outs());

		}

	std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
	                                               StringRef file) override {

		InlinerRewriter.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
		return llvm::make_unique<ExtractorDriver>(CST, RST, CI);

		}

	 bool BeginInvocation(CompilerInstance &CI) override {

		CI.getDiagnostics().setIgnoreAllWarnings(true);
		return true;
		
		}

	void ExecuteAction() override {

		ASTFrontendAction::ExecuteAction();
		Nonce = 0;
		std::vector<FunctionDecl*> CallChain;

		while (!CST.empty()) {

			if (!PerformInline(CST.begin()->first, CallChain))
				return;

			CallChain.clear();

			}

		}

private:
	Rewriter      InlinerRewriter;
	CallSiteTable CST;
	RetStmtTable  RST;
	CodeCache     CC;
	size_t        Nonce;

	SourceLocation ExpandStartLoc(SourceLocation StartLoc) {

		auto& SM = InlinerRewriter.getSourceMgr();

		if(StartLoc.isMacroID()) {
			auto ExpansionRange = SM.getImmediateExpansionRange(StartLoc);
			StartLoc = ExpansionRange.first;
			}

		return StartLoc;

		}

	SourceLocation ExpandEndLoc(SourceLocation EndLoc) {

		auto& SM = InlinerRewriter.getSourceMgr();

		if(EndLoc.isMacroID()) {
			auto ExpansionRange = SM.getImmediateExpansionRange(EndLoc);
			EndLoc = ExpansionRange.second;
			}

		return EndLoc;

		}

	SourceRange ExpandRange(SourceRange Range) {
		Range.setBegin(ExpandStartLoc(Range.getBegin()));
		Range.setEnd(ExpandEndLoc(Range.getEnd()));
		return Range;
		}

	// Generate code snippet for inlining
	bool GenCodeSnippet(FunctionDecl* FuncDecl, std::vector<FunctionDecl*>& CallChain) {

		// Already generated code snippet
		if (CC.find(FuncDecl) != CC.end())
			return true;

		// Inline this function first and then generate code snippet
		if (CST.find(FuncDecl) != CST.end() && !PerformInline(FuncDecl, CallChain))
			return false;

		auto RetStmtRecord = RST.find(FuncDecl);

		// If this function got no return statement
		if (RetStmtRecord == RST.end()) {

			CodeSnippet Body;

			Body.emplace_back(InlinerRewriter.getRewrittenText(
					ExpandRange(FuncDecl->getBody()->getSourceRange())));
			CC.emplace(FuncDecl, std::move(Body));

			return true;

			}

		CodeSnippet CS;
		SourceLocation Front = ExpandStartLoc(FuncDecl->getBody()->getLocStart());

		for (ReturnStmt* RS : RetStmtRecord->second) {

			SourceLocation RSLocStart = ExpandStartLoc(RS->getLocStart());
			SourceLocation RSLocEnd = ExpandEndLoc(RS->getLocEnd());

			CS.emplace_back(InlinerRewriter.getRewrittenText({Front, RSLocStart.getLocWithOffset(-1)}));
			CS.emplace_back(InlinerRewriter.getRewrittenText({RSLocStart.getLocWithOffset(6), RSLocEnd}));
			Front = ExpandEndLoc(RS->getStmtLocEnd());

			}

		CS.emplace_back(InlinerRewriter.getRewrittenText({Front, ExpandEndLoc(FuncDecl->getBody()->getLocEnd())}));
		CC.emplace(FuncDecl, std::move(CS));

		return true;

		}

	bool PerformInline(FunctionDecl* FuncDecl, std::vector<FunctionDecl*>& CallChain) {

		if (!FuncDecl->hasBody()) {

			llvm::errs() << "Skipping declaration without body \""
			             << FuncDecl->getNameInfo().getName().getAsString()
			             << "\"\n";
			return true;

			}

		if (auto It = std::find(CallChain.begin(), CallChain.end(), FuncDecl);
		    It != CallChain.end()) {

			auto& Diag = getCompilerInstance().getDiagnostics();
			auto ErrDiagID = Diag.getCustomDiagID(DiagnosticsEngine::Level::Error, "%0");
			auto NoteDiagID = Diag.getCustomDiagID(DiagnosticsEngine::Level::Note, "call chain: %0 -> %1%0");

			std::string FuncName = FuncDecl->getNameInfo().getName().getAsString();
			std::string Chain;

			Diag.Report(FuncDecl->getNameInfo().getLoc(), ErrDiagID) << "recursion detected";

			for (auto CalledFunc = CallChain.rbegin();
			     *CalledFunc != FuncDecl; ++CalledFunc) {

				Chain = (*CalledFunc)->getNameInfo().getName().getAsString() +
				        " -> " + std::move(Chain);

				}

			Diag.Report(NoteDiagID) << FuncName << Chain;

			return false;

			}

		auto CallSiteRecord = CST.find(FuncDecl);

		// No function call to inline or already inlined
		if (CallSiteRecord == CST.end())
			return true;

		CallChain.emplace_back(FuncDecl);

		// Clang traverse the AST in a DFS manner
		// Subexpressions appear after the main expression, and they should
		// be inlined before the main expression or we will lose the track of
		// source code range
		for (auto CEIt = CallSiteRecord->second.rbegin();
		     CEIt != CallSiteRecord->second.rend();
		     ++CEIt) {

			CallExpr* CE = *CEIt;
			FunctionDecl* Callee = CE->getDirectCallee();

			// May be builtin function
			if (!Callee->hasBody())
				continue;

			if (!GenCodeSnippet(Callee = Callee->getDefinition(), CallChain))
				return false;

			std::string ExitLabel = "__CLPKM_EXIT_" + std::to_string(Nonce++);
			std::string Replace;
			std::string RetVar = " __clpkm_ret_" + std::to_string(Nonce++);
			QualType RetType = Callee->getReturnType();

			auto CCRecord = CC.find(Callee);
			assert(CCRecord != CC.end());

			auto Next = CCRecord->second.begin();
			auto It = Next;

			// Patch all the return statements with do-while and goto
			// Because the ';' after a return statement is not included in
			// its source range, we need the good ol' trick of macro magic
			while (++Next != CCRecord->second.end()) {

				Replace += *It + "do { ";

				if (!RetType->isVoidType())
					Replace += RetVar + " = " + *Next + "; ";

				Replace += "goto " + ExitLabel + "; } while (0)";

				It = ++Next;

				}

			Replace += *It;

			auto ParamIt = Callee->param_begin();
			auto ArgIt = CE->arg_begin();

			// Prepare the arguments for the call expression
			while (ParamIt != Callee->param_end()) {

				Replace = InlinerRewriter.getRewrittenText(ExpandRange((*ParamIt)->getSourceRange())) +
				          " = " +
				          InlinerRewriter.getRewrittenText(ExpandRange((*ArgIt)->getSourceRange())) +
				          ";" + std::move(Replace);

				++ArgIt;
				++ParamIt;

				}

			// Statement expressions from GNU extension
			if (!RetType->isVoidType())
				Replace = RetType.getAsString() + RetVar + "; " +
				          std::move(Replace) +
				          ExitLabel + ": " + RetVar + "; ";
			else
				Replace = std::move(Replace) + ExitLabel + ": ; ";

			// Locally declared labels from GNU extension
			// Must be placed at the beginning of a block
			Replace = "({ __label__ " + ExitLabel + "; " +
			          std::move(Replace) + "})";

			InlinerRewriter.ReplaceText(ExpandRange(CE->getSourceRange()),
			                            Replace);

			}

		// Remove the record so we won't inline twice
		CST.erase(CallSiteRecord);
		CallChain.pop_back();

		return true;

		}

	};



int main(int ArgCount, const char* ArgVar[]) {

	CommonOptionsParser Options(ArgCount, ArgVar, InlinerCategory);
	ClangTool Tool(Options.getCompilations(), Options.getSourcePathList());

	return Tool.run(newFrontendActionFactory<InlinerFrontendAction>().get());

	}
