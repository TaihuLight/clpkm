/*
  Covfefe.cpp

  Generate checkpoint/resume code sequence and update kernel profile (impl)

*/

#include "Covfefe.hpp"
#include "clang/AST/ASTContext.h"

using namespace clang;



Covfefe GenerateCovfefe(LiveVarTracker::liveness&& L, KernelProfile& KP) {

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

Locfefe GenerateLocfefe(std::vector<VarDecl*>& LocDecl, FunctionDecl* FD,
                        KernelProfile& KP) {

	Locfefe LC;

	// Compile time evaluable
	for (VarDecl* VD : LocDecl) {
		
		}

	// Runtime decide
	for (unsigned ParamIdx : KP.LocPtrParamIdx) {
		
		}

	return LC;

	}
