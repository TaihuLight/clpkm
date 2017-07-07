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

	QualType QT = VD->getType();
	TypeInfo TI = VD->getASTContext().getTypeInfo(QT);

	if (QT.getAddressSpace() == LangAS::opencl_local)
		ThePL.back().ReqLocSize += (TI.Width + 7) / 8;

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

	unsigned NumOfParam = FuncDecl->getNumParams();

	// FIXME: I can't figure out a graceful way to do this at the moment
	//        It's uncommon anyway
	if (NumOfParam <= 0) {

		// TODO: remove body of nullary function?
		DiagReport(FuncDecl->getNameInfo().getLoc(),
		           DiagnosticsEngine::Level::Warning,
		           "skipping nullary function");
		return true;

		}

	// Append arguments
	auto LastParam = FuncDecl->getParamDecl(NumOfParam - 1);
	auto InsertCut = LastParam->getSourceRange().getEnd();
	const char* CLPKMParam = ", __global int * __clpkm_hdr, "
	                         "__global char * __clpkm_local, "
	                         "__global char * __clpkm_prv, "
	                         "__const size_t __clpkm_tlv";

	TheRewriter.InsertTextAfterToken(InsertCut, CLPKMParam);

	// Don't traverse function declaration
	if (!FuncDecl->hasBody())
		return true;

	// Inject main control flow
	TheRewriter.InsertTextAfterToken(
		FuncDecl->getBody()->getLocStart(),
		"\n  size_t __clpkm_id = get_global_linear_id();\n"
		"  size_t __clpkm_ctr = 0;\n"
		"  switch (__clpkm_hdr[__clpkm_id]) {\n"
		"  default:  return;\n"
		"  case 1: ;\n");

	TheRewriter.InsertTextBefore(FuncDecl->getBody()->getLocEnd(),
	                             "\n  }\n  __clpkm_hdr[__clpkm_id] = 0;\n");

	// Preparation for traversal
	ThePL.emplace_back(FuncDecl->getNameInfo().getName().getAsString(), NumOfParam);
	LVT.SetContext(FuncDecl);
	CostCounter = 0;
	Nonce = 1;

	// Traverse
	bool Ret = RecursiveASTVisitor<Instrumentor>::TraverseFunctionDecl(FuncDecl);

	// Cleanup
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

	Covfefe C = GenerateCovfefe(Body);
	std::string ThisNonce = std::to_string(++Nonce);
	std::string InstCR =
			" __clpkm_ctr += " + std::to_string(NewCost - OldCost) + ";"
			" case " + ThisNonce + ":"
			" if (__clpkm_ctr > __clpkm_tlv) {"
				"  __clpkm_hdr[__clpkm_id] = " + ThisNonce + "; " +
				std::move(C.first) + " return;"
			" } else if (__clpkm_ctr <= 0) { " +
				std::move(C.second) + " } ";

	if (isa<CompoundStmt>(Body))
		TheRewriter.InsertTextBefore(Body->getLocEnd(), InstCR);
	else {

		TheRewriter.InsertTextBefore(Body->getLocStart(), " { ");
		TheRewriter.InsertTextAfterToken(Body->getStmtLocEnd(),
		                                 InstCR += " } ");

		}

	return true;

	}

auto Instrumentor::GenerateCovfefe(Stmt* S) -> Covfefe {

	// The first is for checkpoint, and the second is for resume
	Covfefe C;
	size_t ReqPrvSize = 0;

	for (VarDecl* VD : this->LVT.GenLivenessAfter(S)) {

		QualType QT = VD->getType();
		TypeInfo TI = VD->getASTContext().getTypeInfo(QT);

		switch (QT.getAddressSpace()) {
		// No need to store global stuff
		case LangAS::opencl_global:
		case LangAS::opencl_constant:
			break;

		case LangAS::opencl_local:
			// TODO
			break;

		// clang::LangAS::Default, IIUC, private
		case 0:
			// New scope to deal with bypassing initialization
			{
				const char* VarName = VD->getIdentifier()->getNameStart();
				size_t Size = (TI.Width + 7) / 8;

				std::string P = "(__clpkm_prv+" +
				                std::to_string(ReqPrvSize) +
				                ", &" + std::string(VarName) + ", " +
				                std::to_string(Size) + "); ";
				C.first += "__clpkm_store_private";
				C.first += P;
				C.second += "__clpkm_load_private";
				C.second += std::move(P);
				ReqPrvSize += Size;

				}
			break;

		default:
			llvm_unreachable("Unexpected address space :(");

			}

		}

	// Update max requested size
	size_t OldReqSize = ThePL.back().ReqPrvSize;
	ThePL.back().ReqPrvSize = std::max(OldReqSize, ReqPrvSize);

	return C;

	}
