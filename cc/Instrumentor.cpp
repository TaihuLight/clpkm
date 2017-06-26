/*
  Instrumentor.cpp

  The code injector of CLPKMCC (impl).

*/

#include "Instrumentor.hpp"
#include <string>

using namespace clang;



bool Instrumentor::VisitSwitchStmt(SwitchStmt* SS) {

		DiagReport(SS->getLocStart(), DiagnosticsEngine::Level::Error,
		           "switch is not supported");

		return false;

		}

bool Instrumentor::VisitStmt(Stmt* S) {

	CostCounter++;
	return true;

	}

bool Instrumentor::VisitReturnStmt(ReturnStmt* RS) {

	if (RS != nullptr) {

		TheRewriter.InsertTextBefore(RS->getLocStart(),
		                             "__clpkm_hdr[__clpkm_id] = 0;");

		}

	return true;

	}

bool Instrumentor::VisitVarDecl(VarDecl* VD) {

	if (VD != nullptr)
		LVT.AddTrack(VD);

	return true;

	}

bool Instrumentor::VisitCallExpr(CallExpr* CE) {

	if (CE == nullptr || CE->getDirectCallee() == nullptr)
		return true;

	if (CE->getDirectCallee()->getNameInfo().getName().getAsString() !=
	    "barrier")
		return true;

	// TODO

	return true;

	}

bool Instrumentor::TraverseForStmt(ForStmt* FS) {

	if (FS == nullptr)
		return true;

	size_t OldCost = CostCounter;
	RecursiveASTVisitor<Instrumentor>::TraverseForStmt(FS);

	return PatchLoopBody(OldCost, CostCounter,
	                     FS, FS->getCond(), FS->getBody());

	}

bool Instrumentor::TraverseDoStmt(DoStmt* DS) {

	if (DS == nullptr)
		return true;

	size_t OldCost = CostCounter;
	RecursiveASTVisitor<Instrumentor>::TraverseDoStmt(DS);

	return PatchLoopBody(OldCost, CostCounter,
	                     DS, DS->getCond(), DS->getBody());

	}

bool Instrumentor::TraverseWhileStmt(WhileStmt* WS) {

	if (WS == nullptr)
		return true;

	size_t OldCost = CostCounter;
	RecursiveASTVisitor<Instrumentor>::TraverseWhileStmt(WS);

	return PatchLoopBody(OldCost, CostCounter,
	                     WS, WS->getCond(), WS->getBody());

	}

// 1.   Patch arguments
// 2.   Re-arrange BBs
// 3.   Insert Checkpoint/Resume codes
bool Instrumentor::TraverseFunctionDecl(FunctionDecl* FuncDecl) {

	if (FuncDecl == nullptr || !FuncDecl->hasAttr<OpenCLKernelAttr>())
		return true;

	// FIXME: I can't figure out a graceful way to do this at the moment
	//        It's uncommon anyway
	if (auto ParamSize = FuncDecl->param_size(); ParamSize <= 0) {

			// TODO: remove body of nullary function?
			DiagReport(FuncDecl->getNameInfo().getLoc(), 
			           DiagnosticsEngine::Level::Warning,
			           "skipping nullary function");
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
	LVT.SetContext(FuncDecl);
	bool Ret = RecursiveASTVisitor<Instrumentor>::TraverseFunctionDecl(FuncDecl);
	LVT.EndContext();

	return Ret;

	}

bool Instrumentor::PatchLoopBody(size_t OldCost, size_t NewCost,
                   Stmt* Loop, Expr* Cond, Stmt* Body) {

	CostCounter = OldCost;

	// If this condition is compiler-time evaluable
	// Note: Cond could be nullptr in the case like:
	//     for (;;) ...
	if (bool Result;
	    Cond != nullptr && Cond->isEvaluatable(TheCI.getASTContext()) &&
	    Cond->EvaluateAsBooleanCondition(Result, TheCI.getASTContext())) {

		// This loop will never repeat
		if (!Result)
			return true;

		DiagReport(Cond->getLocStart(), DiagnosticsEngine::Level::Warning,
		           "infinite loop");

		}

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
		TheRewriter.InsertTextAfterToken(Body->getStmtLocEnd(),
		                                 InstCR += " } ");

		}

	return true;

	}
