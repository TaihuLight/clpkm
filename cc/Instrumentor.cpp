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
class FuncInfoCollector : public RecursiveASTVisitor<FuncInfoCollector> {
public:
	FuncInfoCollector(std::vector<DeclStmt*>& LD, size_t& BC)
	: LocDecl(LD), BarrierCount(BC) { BarrierCount = 0; }

	bool VisitDeclStmt(DeclStmt* DS) {
		if (DS == nullptr || DS->getDeclGroup().isNull())
			return true;
		VarDecl* VD = dyn_cast_or_null<VarDecl>(*DS->decl_begin());
		if (VD == nullptr)
			return true;
		QualType QT = VD->getType();
		if (QT.getAddressSpace() == LangAS::opencl_local)
			LocDecl.emplace_back(DS);
		return true;
		}

	bool VisitCallExpr(CallExpr* CE) {
		if (CE == nullptr || CE->getDirectCallee() == nullptr)
			return true;
		if (CE->getDirectCallee()->getNameInfo().getName().getAsString() ==
		    "barrier")
			++BarrierCount;
		return true;
		}

private:
	std::vector<DeclStmt*>& LocDecl;
	size_t& BarrierCount;

	};
}



bool Instrumentor::VisitSwitchStmt(SwitchStmt* SS) {

		DiagReport(SS->getLocStart(), DiagnosticsEngine::Level::Error,
		           "switch is not supported");

		return false;

		}

bool Instrumentor::VisitStmt(Stmt* S) {

	// TODO: fine-grained cost evaluation
	CostCounter++;
	return true;

	}

bool Instrumentor::VisitReturnStmt(ReturnStmt* RS) {

	if (RS != nullptr) {

		// PP should have added braces for return statements
		// Note: The range involves not the semicolon
		TheRewriter.ReplaceText({RS->getLocStart(), RS->getLocEnd()},
		                        " do { __clpkm_hdr[__clpkm_id] = 0;"
		                        " goto __CLPKM_SV_LOC_AND_RET; } while(0)");

		}

	return true;

	}

// Remove variable declaration located at local memory
bool Instrumentor::VisitDeclStmt(DeclStmt* DS) {
		if (DS == nullptr || DS->getDeclGroup().isNull())
			return true;
		VarDecl* VD = dyn_cast_or_null<VarDecl>(*DS->decl_begin());
		if (VD == nullptr)
			return true;
		QualType QT = VD->getType();
		if (QT.getAddressSpace() == LangAS::opencl_local)
			TheRewriter.RemoveText({DS->getLocStart(), DS->getLocEnd()});
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

	std::string OrigBarrier =
			TheRewriter.getRewrittenText(CE->getSourceRange());

	// If CLK_LOCAL_MEM_FENCE and CLK_GLOBAL_MEM_FENCE are macro definitions, and
	// they are made used as the argument of this call,
	// getRewrittenText(CE->getArg(0)->getSourceRange()) returns empty string
	const auto RParenLoc = OrigBarrier.find_last_of(')');
	assert(RParenLoc != std::string::npos);
	OrigBarrier.replace(OrigBarrier.begin() + RParenLoc, OrigBarrier.end(),
	                    " | CLK_LOCAL_MEM_FENCE)");

	std::string BarrierStop =
			"__clpkm_barrier_stop[" + std::to_string(BarrierIdx++) + "]";

	Covfefe C = GenerateCovfefe(LVT.GenLivenessAfter(CE), ThePL.back());
	std::string ThisNonce = std::to_string(++Nonce);

	std::string InstCR =
			" do {\n"
			"   atomic_dec(&" + BarrierStop + ");\n"
			"   barrier(CLK_LOCAL_MEM_FENCE);\n"
			"   uint __remain_count = " + BarrierStop + ";\n"
			"   barrier(CLK_LOCAL_MEM_FENCE);\n"
			"   atomic_inc(&" + BarrierStop + ");\n"
			"   if (__remain_count != 0) { \n"
			"     __clpkm_hdr[__clpkm_id] = -" + ThisNonce + ";\n" +
			      std::move(C.first) +
			"     goto __CLPKM_SV_LOC_AND_RET;"
			"   }\n"
			"   " + OrigBarrier + ";\n"
			"   if (0) case " + ThisNonce + ": {\n" +
			      std::move(C.second) +
			"   }\n"
			" } while (0)";

	TheRewriter.ReplaceText(CE->getSourceRange(), InstCR);

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
	const char* CLPKMParam = ", __global int * restrict __clpkm_metadata, "
	                         "__global char * restrict __clpkm_local, "
	                         "__global char * restrict __clpkm_prv, "
	                         "__const uint __clpkm_tlv";

	TheRewriter.InsertTextAfterToken(InsertCut, CLPKMParam);

	// Don't traverse function declaration
	if (!FuncDecl->hasBody())
		return true;

	// Put a new entry into the kernel profile list
	auto& PLEntry = ThePL.emplace_back(FuncName, NumOfParam);

	std::string DynSzLocBufSize = "0";

	// Collect info of kernel parameters that point to local memory
	// The size of these buffers are not deterministic here
	unsigned ParamIdx = 0;
	for (ParmVarDecl* PVD : FuncDecl->parameters()) {
		if (QualType QT = PVD->getType(); QT->isPointerType()) {
			QualType PointeeQT = QT->getPointeeType();
			if (PointeeQT.getAddressSpace() == LangAS::opencl_local) {
				DynSzLocBufSize += " + __clpkm_dloc_sz_tbl[" +
				                   std::to_string(PLEntry.LocPtrParamIdx.size()) +
				                   "]";
				PLEntry.LocPtrParamIdx.emplace_back(ParamIdx);
				}
//			QualType PointeeQT = QT->getPointeeType();
//			llvm::errs() << "---> "<< PointeeQT.getAsString() << ' '
//			             << PVD->getIdentifier()->getNameStart() << '\n';
			}
		++ParamIdx;
		}

	std::vector<DeclStmt*> LocDecl;
	size_t BarrierCount = 0;

	// Collect local decl and number of barrier
	{
		FuncInfoCollector V(LocDecl, BarrierCount);
		V.TraverseFunctionDecl(FuncDecl);
		}

	auto Locfefe = GenerateLocfefe(LocDecl, TheRewriter, FuncDecl, PLEntry);

	auto InitBarrierStop = [&]() -> std::string {
		if (BarrierCount == 0)
			return std::string();
		const std::string StrBarrierCount = std::to_string(BarrierCount);
		return "  __local uint __clpkm_barrier_stop[" + StrBarrierCount + "];\n"
		       "  for (size_t __idx = __clpkm_loc_id; "
		              "__idx < " + StrBarrierCount + "; "
		              "__idx += __clpkm_grp_size)\n"
		       "    __clpkm_barrier_stop[__idx] = __clpkm_grp_size;\n";
		}();

	auto InitFinalStop = [&]() -> const char* {
		if (Locfefe.first.empty())
			return "";
		return "  __local uint __clpkm_final_stop;\n"
		       "  if (__clpkm_loc_id == 0)\n"
		       "    __clpkm_final_stop = __clpkm_grp_size;\n";
		}();

	const char* InitBarrier = (Locfefe.second.size() || InitBarrierStop.size())
	                          ? "barrier(CLK_LOCAL_MEM_FENCE);" : "";

	// Inject main control flow
	TheRewriter.InsertTextAfterToken(
		FuncDecl->getBody()->getLocStart(),
		"\n  __global const uint* __clpkm_dloc_sz_tbl = (__global uint*) __clpkm_metadata;\n"
		"  __global int* __clpkm_hdr = __clpkm_metadata + " + std::to_string(PLEntry.LocPtrParamIdx.size()) + ";\n"
		"  size_t __clpkm_id = 0; // global work-item id \n"
		"  size_t __clpkm_grp_id = 0; // work-group id \n"
		"  size_t __clpkm_loc_id = 0; // local work-item id\n"
		"  size_t __clpkm_grp_size = 0; // work-group size \n"
		"  uint __clpkm_ctr = 0; // cost counter\n"
		"  // Compute linear IDs and adjust live value buffer\n"
		"  __get_linear_id(&__clpkm_id, &__clpkm_grp_id,\n"
		"                  &__clpkm_loc_id, &__clpkm_grp_size);\n"
		"  __clpkm_prv += __clpkm_id * " + ReqPrvSizeVar + ";\n"
		"  __clpkm_local += __clpkm_grp_id * (" + ReqLocSizeVar + " + " +
		                                      DynSzLocBufSize + ");\n"
		"  // Initialize barrier stop\n" +
		std::move(InitBarrierStop) + InitFinalStop +
		"  // Load live values for variables locate in local memory\n" +
		std::move(Locfefe.second) +
		InitBarrier +
		"  switch (__clpkm_hdr[__clpkm_id]) {\n"
		"  default: goto __CLPKM_SV_LOC_AND_RET;\n"
		"  case 1: ;\n");

	TheRewriter.InsertTextBefore(FuncDecl->getBody()->getLocEnd(),
	                             " } // switch\n"
	                             " __clpkm_hdr[__clpkm_id] = 0;\n"
	                             " __CLPKM_SV_LOC_AND_RET: ;\n" +
	                             std::move(Locfefe.first));

	// Preparation for traversal
	LVT.SetContext(FuncDecl);
	CostCounter = 0;
	Nonce = 1;
	BarrierIdx = 0;

	// Traverse
	bool Ret = RecursiveASTVisitor<Instrumentor>::TraverseFunctionDecl(FuncDecl);

	// Cleanup
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

	Covfefe C = GenerateCovfefe(LVT.GenLivenessAfter(Body), ThePL.back());
	std::string ThisNonce = std::to_string(++Nonce);
	std::string InstCR =
			" __clpkm_ctr += " + std::to_string(NewCost - OldCost) + ";"
			" if (__clpkm_ctr > __clpkm_tlv) {"
				" __clpkm_hdr[__clpkm_id] = " + ThisNonce + "; " +
				std::move(C.first) + " goto __CLPKM_SV_LOC_AND_RET;"
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



// Static member functions
auto Instrumentor::GenerateCovfefe(LiveVarTracker::liveness&& L,
                                   KernelProfile& KP) -> Covfefe {

	Covfefe C;
	size_t ReqPrvSize = 0;

	for (VarDecl* VD : L) {

		QualType QT = VD->getType();
		TypeInfo TI = VD->getASTContext().getTypeInfo(QT);

		switch (QT.getAddressSpace()) {
		// clang::LangAS::Default, IIUC, private
		case 0: {
				const char* VarName = VD->getIdentifier()->getNameStart();
				size_t Size = (TI.Width + 7) / 8;

				// If it's profitable, pad leading buffer
				if (Size >= 4)
					ReqPrvSize = (ReqPrvSize + 3) & ~static_cast<size_t>(0b11);

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

		case LangAS::opencl_global:
		case LangAS::opencl_constant:
		case LangAS::opencl_local:
			break;

		default:
			llvm_unreachable("Unexpected address space :(");

			}

		}

	// Update max requested size
	KP.ReqPrvSize = std::max(KP.ReqPrvSize, ReqPrvSize);

	return C;

	}

auto Instrumentor::GenerateLocfefe(std::vector<DeclStmt*>& LocDecl, Rewriter& R,
                                   FunctionDecl* FD,
                                   KernelProfile& KP) -> Locfefe {

	unsigned NumOfEvent = 0;
	size_t   ReqLocSize = 0;

	// {store, load}
	Locfefe LC;
	std::string Decl;

	// Compile time evaluable
	for (DeclStmt* DS : LocDecl) {

		// Move forward declaration
		Decl += R.getRewrittenText({DS->getLocStart(), DS->getLocEnd()});

		for (auto It = DS->decl_begin(); It != DS->decl_end(); ++It) {

			VarDecl* VD = dyn_cast_or_null<VarDecl>(*It);
			QualType QT = VD->getType();
			TypeInfo TI = VD->getASTContext().getTypeInfo(QT);
			size_t Size = (TI.Width + 7) / 8;
			size_t PaddedSize = (Size + 3) & ~static_cast<size_t>(0b11);

			std::string StrLocArg =
					"(__local char*)&" +
					std::string(VD->getIdentifier()->getNameStart());
			std::string StrGblArg =
					"__clpkm_local + " + std::to_string(ReqLocSize);
			std::string StrSize = std::to_string(Size);

			LC.first += "__clpkm_store_local(" + StrGblArg +
			            ", " + StrLocArg + ", " + StrSize + ", __clpkm_exit_id, __clpkm_batch_sz); ";
			LC.second += "__clpkm_cp_ev = async_work_group_copy(" +
			             std::move(StrLocArg) + ", " + std::move(StrGblArg) +
			             ", " + std::move(StrSize) + ", __clpkm_cp_ev); ";

			++NumOfEvent;
			ReqLocSize += PaddedSize;

			}
		}

	KP.ReqLocSize += ReqLocSize;

	std::string StrOffset = std::to_string(ReqLocSize);

	// Set up offset for runtime decided local size
	LC.first += "size_t __clpkm_dloc_offset = " + StrOffset + "; ";
	LC.second += "size_t __clpkm_dloc_offset = " + StrOffset + "; ";

	size_t IdxAcc = 0;

	// Runtime decide
	for (unsigned ParamIdx : KP.LocPtrParamIdx) {

			std::string StrLocArg = "(__local char*)" + std::string(
							FD->getParamDecl(ParamIdx)->getIdentifier()->getNameStart());
			std::string StrSize = "__clpkm_dloc_sz_tbl[" + std::to_string(IdxAcc) + "]";

			LC.first += "__clpkm_store_local("
			            "__clpkm_local + __clpkm_dloc_offset, " +
			            StrLocArg + ", " + StrSize + ", __clpkm_exit_id, __clpkm_batch_sz); "
			            "__clpkm_dloc_offset += " + StrSize + "; ";
			LC.second += "__clpkm_cp_ev = async_work_group_copy(" +
			             std::move(StrLocArg) + ", "
			             "__clpkm_local + __clpkm_dloc_offset, " +
			             std::move(StrSize) + ", __clpkm_cp_ev); "
			             "__clpkm_dloc_offset += " + StrSize + "; ";

		++IdxAcc;
		++NumOfEvent;

		}

	// Nothing to store
	if (NumOfEvent == 0)
		return Locfefe();

	// Sync before store to live value buffer
	LC.first = " uint __clpkm_batch_sz = __clpkm_final_stop; "
	           " barrier(CLK_LOCAL_MEM_FENCE);"
	           " uint __clpkm_exit_id = atomic_dec(&__clpkm_final_stop) - 1;"
	           " barrier(CLK_LOCAL_MEM_FENCE);"
	           " if (__clpkm_final_stop != 0)"
	           "   return;" +
	           std::move(LC.first);

	LC.second = std::move(Decl) +
	            " if (__clpkm_hdr[__clpkm_id] != 1) { "
	            "   event_t __clpkm_cp_ev = 0; " +
	            std::move(LC.second) +
	            "   wait_group_events(1, &__clpkm_cp_ev); "
	            "   } ";

	return LC;

	}
