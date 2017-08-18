/*
  Instrumentor.cpp

  The code injector of CLPKMCC (impl).

*/

#include "Instrumentor.hpp"
#include <string>
#include <vector>

using namespace clang;



namespace {
// Helper class to collect info of variables located in local memory
class LocalDeclVisitor : public RecursiveASTVisitor<LocalDeclVisitor> {
public:
	LocalDeclVisitor(std::vector<VarDecl*>& LD)
	: LocDecl(LD) { }

	bool VisitDeclStmt(DeclStmt* DS) {
		if (DS == nullptr)
			return true;
		VarDecl* VD = dyn_cast_or_null<VarDecl>(DS->getSingleDecl());
		if (VD == nullptr)
			return true;
		QualType QT = VD->getType();
		if (QT.getAddressSpace() == LangAS::opencl_local)
			LocDecl.emplace_back(VD);
		return true;
		}

private:
	std::vector<VarDecl*>& LocDecl;
	};
}



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

		// PP should have added braces for return statements
		TheRewriter.ReplaceText({RS->getLocStart(), RS->getLocEnd()},
		                        " __clpkm_hdr[__clpkm_id] = 0;"
		                        " goto " + ExitLabel);

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

bool Instrumentor::TraverseFunctionDecl(FunctionDecl* FuncDecl) {

	if (FuncDecl == nullptr || !FuncDecl->hasAttr<OpenCLKernelAttr>())
		return true;

	std::string FuncName = FuncDecl->getNameInfo().getName().getAsString();
	std::string ReqPrvSizeVar = "__clpkm_req_prv_size_" + FuncName;
	std::string ReqLocSizeVar = "__clpkm_req_loc_size_" + FuncName;

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
	const char* CLPKMParam = ", __global int * restrict __clpkm_hdr, "
	                         "__global char * restrict __clpkm_local, "
	                         "__global char * restrict __clpkm_prv, "
	                         "__const uint __clpkm_tlv";

	TheRewriter.InsertTextAfterToken(InsertCut, CLPKMParam);

	// Don't traverse function declaration
	if (!FuncDecl->hasBody())
		return true;

	// Put a new entry into the kernel profile list
	auto& PLEntry = ThePL.emplace_back(FuncName, NumOfParam);

	// Collect info of kernel parameters that point to local memory
	// The size of these buffers are not deterministic here
	unsigned ParamIdx = 0;
	for (ParmVarDecl* PVD : FuncDecl->parameters()) {
		if (QualType QT = PVD->getType(); QT->isPointerType()) {
			if (QualType PointeeQT = QT->getPointeeType();
			    PointeeQT.getAddressSpace() == LangAS::opencl_local)
				PLEntry.LocPtrParamIdx.emplace_back(ParamIdx);
//			QualType PointeeQT = QT->getPointeeType();
//			llvm::errs() << "---> "<< PointeeQT.getAsString() << ' '
//			             << PVD->getIdentifier()->getNameStart() << '\n';
			}
		++ParamIdx;
		}

	// Inject main control flow
	TheRewriter.InsertTextAfterToken(
		FuncDecl->getBody()->getLocStart(),
		"\n  size_t __clpkm_id = 0;\n"
		"  size_t __clpkm_grp_id = 0;\n"
		"  __get_linear_id(&__clpkm_id, &__clpkm_grp_id);\n"
		"  uint __clpkm_ctr = 0;\n"
		"  __clpkm_prv += __clpkm_id * " + std::string(ReqPrvSizeVar) + ";\n"
		"  switch (__clpkm_hdr[__clpkm_id]) {\n"
		"  default:  return;\n"
		"  case 1: ;\n");

	ExitLabel = "__CLPKM_SV_LOC_AND_RET";

	TheRewriter.InsertTextBefore(FuncDecl->getBody()->getLocEnd(),
	                             " } // switch\n"
	                             " __clpkm_hdr[__clpkm_id] = 0;\n" +
	                             ExitLabel + ": ;\n"
	                             /* TODO: store local */);

	// Preparation for traversal
	LVT.SetContext(FuncDecl);
	CostCounter = 0;
	Nonce = 1;

	// Traverse
	bool Ret = RecursiveASTVisitor<Instrumentor>::TraverseFunctionDecl(FuncDecl);

	// Cleanup
	ExitLabel.clear();
	LVT.EndContext();

	// Emit max requested size
	std::string ReqSizeDeclStr =
			"\n__constant size_t " + ReqPrvSizeVar + " = " +
				std::to_string(PLEntry.ReqPrvSize) + ";"
			"\n__constant size_t " + ReqLocSizeVar + " = " +
				std::to_string(PLEntry.ReqLocSize) + ";";

	TheRewriter.InsertTextBefore(FuncDecl->getLocStart(), ReqSizeDeclStr);

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
			" if (__clpkm_ctr > __clpkm_tlv) {"
				" __clpkm_hdr[__clpkm_id] = " + ThisNonce + "; " +
				std::move(C.first) + " goto " + ExitLabel + ";"
			" } if (0) case " + ThisNonce + ": {" +
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
