/*
  LiveVarHelper.cpp

  A Simple wrapper (impl).

*/

#include "LiveVarTracker.hpp"

using namespace clang;



bool LiveVarTracker::SetContext(const Decl* D) {

	this->EndContext();
#if LLVM_VERSION_MAJOR >= 6
	Manager = new AnalysisDeclContextManager(D->getASTContext());
#else
	Manager = new AnalysisDeclContextManager;
#endif
	Context = Manager->getContext(D);

	// Or it will skil sub-exprs
	Context->getCFGBuildOptions().setAllAlwaysAdd();

	LiveVar = LiveVariables::computeLiveness(*Context,
	                                         /* killAtAssign */ false);
	Map = Context->getCFGStmtMap();

	// TODO
	return true;

	}

void LiveVarTracker::EndContext() {

	Tracker.clear();
	delete LiveVar;
	delete Manager;

	Manager = nullptr;
	Context = nullptr;
	Map = nullptr;
	LiveVar = nullptr;
	Scope = 0;

	}

bool LiveVarTracker::IsLiveAfter(VarDecl* VD, Stmt* S) const {

	if (VD == nullptr || S == nullptr || LiveVar == nullptr || Map == nullptr)
		return false;

	CFGBlock* B = Map->getBlock(S);

	if (B == nullptr)
		llvm_unreachable("Stmt outside of CFGMap???");

	return LiveVar->isLive(B, VD);

	}

void LiveVarTracker::PopScope() {

	auto It = Tracker.begin();

	while (It != Tracker.end()) {
		if (It->second >= Scope)
			It = Tracker.erase(It);
		else
			++It;
		}

	--Scope;

	}
