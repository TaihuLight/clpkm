/*
  LiveVarHelper.hpp

  A Simple wrapper.

*/

#include "clang/Analysis/Analyses/LiveVariables.h"
#include "clang/Analysis/CFGStmtMap.h"
#include <unordered_set>

class LiveVarTracker {
private:
	using tracker_type = std::unordered_set<clang::VarDecl*>;
	using tracker_iter = tracker_type::iterator;

public:
	class iterator {
		iterator(LiveVarTracker* InitLVT, clang::Stmt* InitS,
		         tracker_iter InitIt)
		: LVT(InitLVT), S(InitS), It(InitIt) { }

	public:
		iterator() = delete;
		iterator(const iterator& ) = default;
		iterator& operator=(const iterator& ) = default;

		auto& operator*() { return *It; }
		auto* operator->() { return &(*It); }
		iterator operator++(int) { auto Old(*this); ++(*this); return Old; }

		iterator& operator++() {
			while(++It != LVT->Tracker.end() && !LVT->IsLiveAfter(*It, S));
			return (*this);
			}

		bool operator==(const iterator& RHS) const { return It == RHS.It; }
		bool operator!=(const iterator& RHS) const { return It != RHS.It; }

	private:
		LiveVarTracker* LVT;
		clang::Stmt*    S;
		tracker_iter    It;

		friend class LiveVarTracker;

		};

	// Helper class to generate iterator
	class liveness {
	private:
		liveness(LiveVarTracker* InitLVT, clang::Stmt* InitS)
		: LVT(InitLVT), S(InitS) { }

	public:
		liveness() = delete;
		liveness(const liveness& ) = default;
		liveness& operator=(const liveness& ) = default;

		iterator begin() const {
			tracker_iter It = LVT->Tracker.begin();
			while (It != LVT->Tracker.end() && !LVT->IsLiveAfter(*It, S))
				++It;
			return iterator(LVT, S, It);
			}

		iterator end() const {
			return iterator(LVT, S, LVT->Tracker.end());
			}

	private:
		LiveVarTracker* LVT;
		clang::Stmt* S;

		friend class LiveVarTracker;

		};

	// Default stuff
	LiveVarTracker() = default;
	~LiveVarTracker() { this->EndContext(); }
	LiveVarTracker(const LiveVarTracker& ) = delete;

	// Set context, e.g. FunctionDecl
	bool SetContext(const clang::Decl* );
	void EndContext();
	bool HasContext() const noexcept { return (Map != nullptr); }

	// Variable declarations to track liveness
	bool AddTrack(clang::VarDecl* VD) { return Tracker.emplace(VD).second; }
	bool RemoveTrack(clang::VarDecl* VD) { return (Tracker.erase(VD) > 0); }
	void ClearTrack() { Tracker.clear(); }

	liveness GenLivenessAfter(clang::Stmt* S) { return liveness(this, S); }

private:
	bool IsLiveAfter(clang::VarDecl* , clang::Stmt* ) const;

	clang::AnalysisDeclContextManager* Manager;
	clang::AnalysisDeclContext*        Context;
	clang::CFGStmtMap*                 Map;
	clang::LiveVariables*              LiveVar;

	tracker_type Tracker;

	};
