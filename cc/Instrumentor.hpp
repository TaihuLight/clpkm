/*
  Instrumentor.hpp

  The code injector of CLPKMCC.

*/

#ifndef __CLPKM__INSTRUMENTOR_HPP__
#define __CLPKM__INSTRUMENTOR_HPP__

#include "KernelProfile.hpp"
#include "LiveVarTracker.hpp"

#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Rewrite/Core/Rewriter.h"

#include <string>
#include <utility>



class Instrumentor : public clang::RecursiveASTVisitor<Instrumentor> {
public:
	Instrumentor(clang::Rewriter& R, clang::CompilerInstance& CI,
	             ProfileList& PL)
	: TheRewriter(R), TheCI(CI), ThePL(PL) { }

	// Forbid switch
	bool VisitSwitchStmt(clang::SwitchStmt* );

	// Update cost counter
	bool VisitStmt(clang::Stmt* );

	// Set header to 0 before return
	bool VisitReturnStmt(clang::ReturnStmt* );

	// Collect VarDecl to compute liveness
	bool VisitDeclStmt(clang::DeclStmt* );
	bool VisitVarDecl(clang::VarDecl* );

	// Patch barrier
	bool VisitCallExpr(clang::CallExpr* );

	// Patch loops
	bool TraverseForStmt(clang::ForStmt* );
	bool TraverseDoStmt(clang::DoStmt* );
	bool TraverseWhileStmt(clang::WhileStmt* );

	// Patch arguments, main control flow, etc
	bool TraverseFunctionDecl(clang::FunctionDecl* );

private:
	template <unsigned N>
	clang::DiagnosticBuilder DiagReport(clang::SourceLocation SL,
	                                    clang::DiagnosticsEngine::Level LV,
	                                    const char (&FormatStr)[N]) {
		auto& TheDiag = TheCI.getDiagnostics();
		auto  DiagID = TheDiag.getCustomDiagID(LV, FormatStr);
		return TheDiag.Report(SL, DiagID);
		}

	bool PatchLoopBody(size_t OldCost, size_t NewCost, clang::Stmt* Loop,
	                   clang::Expr* Cond, clang::Stmt* Body);

	clang::Rewriter&         TheRewriter;
	clang::CompilerInstance& TheCI;
	ProfileList&             ThePL;

	LiveVarTracker LVT;

	size_t CostCounter;
	size_t Nonce;

	// A covfefe is a checkpoint/resume code sequence for variables located in
	// private memory
	using Covfefe = std::pair<std::string, std::string>;
	static Covfefe GenerateCovfefe(LiveVarTracker::liveness&& , KernelProfile& );

	// Locfefe is the counterpart of local memory
	using Locfefe = std::pair<std::string, std::string>;
	static Locfefe GenerateLocfefe(std::vector<clang::VarDecl*>& ,
	                               clang::FunctionDecl* , KernelProfile& );

	};



#endif
