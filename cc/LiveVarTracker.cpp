/*
  LiveVarHelper.cpp

  A Simple wrapper (impl).

*/

#include "LiveVarTracker.hpp"



bool LiveVarTracker::SetContext(const clang::Decl* D) {

	this->EndContext();
	Manager = new clang::AnalysisDeclContextManager;
	Context = Manager->getContext(D);

	// Or it will skil sub-exprs
	Context->getCFGBuildOptions().setAllAlwaysAdd();

	LiveVar = clang::LiveVariables::computeLiveness(
			*Context,	/* killAtAssign */ false);
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

	}

bool LiveVarTracker::IsLiveAfter(clang::VarDecl* VD, clang::Stmt* S) const {

	if (VD == nullptr || S == nullptr || LiveVar == nullptr || Map == nullptr)
		return false;

	clang::CFGBlock* B = Map->getBlock(S);

	// The given Stmt is not in the CFGMap???
	if (B == nullptr)
		return false;

	return LiveVar->isLive(B, VD);

	}
