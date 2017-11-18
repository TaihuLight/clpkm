/*
  ScheduleService.hpp

  Helper class that provides the service from CLPKM daemon

*/

#ifndef __CLPKM__SCHEDULE_SERVICE_HPP__
#define __CLPKM__SCHEDULE_SERVICE_HPP__

#include "TaskKind.hpp"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <systemd/sd-bus.h>



namespace CLPKM {

class ScheduleService;
ScheduleService& getScheduleService(void);

class ScheduleService {
public:
	enum class priority : bool {
		LOW = false,
		HIGH = true
		};

	class SchedGuard {
	public:
		SchedGuard(SchedGuard&& G)
		: Kind(G.Kind) { G.Kind = task_kind::NUM_OF_TASK_KIND; }

		~SchedGuard() {
			if (Kind < task_kind::NUM_OF_TASK_KIND)
				getScheduleService().SchedEnd(Kind);
			}

	private:
		SchedGuard(task_kind K)
		: Kind(K) { getScheduleService().SchedStart(Kind); }

		SchedGuard() = delete;
		SchedGuard(const SchedGuard& ) = delete;
		const SchedGuard& operator=(const SchedGuard& ) = delete;

		task_kind Kind;
		friend class ScheduleService;

		};

	const std::string& getCompilerPath() { return CompilerPath; }

	uint64_t getCRThreshold() { return Threshold; }
	priority getPriority() { return Priority; }

	// Call this function when the process want to do some task
	SchedGuard Schedule(task_kind K) { return SchedGuard(K); }
	// For async APIs, like clEnqueue series
	void ScheduleOnEvent(task_kind K, cl_event* E);

	// Shutdown IPC worker thread
	void Terminate();

private:
	ScheduleService& operator=(const ScheduleService& ) = delete;
	ScheduleService(const ScheduleService& ) = delete;

	// Internal state of this process
	bool     IsOnTerminate;
	bool     IsOnSystemBus;
	priority Priority;

	sd_bus*      Bus;
	sd_bus_slot* Slot;

	// Task related stuff
	std::thread IPCWorker;

	// Data retrieved from the service
	std::string CompilerPath;
	uint64_t    Threshold;

	// For low priority tasks, RunLevel is either 0 or 1, indicating if there is
	// any high priority task running
	// For high priority tasks, RunLevel is the number of tasks running in this
	// process
	std::atomic<unsigned> RunLevel;

	// Condition of RunLevel
	std::mutex              Mutex;
	std::condition_variable CV;

	// Internal functions
	ScheduleService();
	~ScheduleService();

	void StartBus();

	void SchedStart(task_kind );
	void SchedEnd(task_kind );

	void LowPrioProcWorker();
	void HighPrioProcWorker();

	friend ScheduleService& getScheduleService(void);

	};

} // namespace CLPKM



#endif
