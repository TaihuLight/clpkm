/*
  ScheduleService.hpp

  Helper class that provides the service from CLPKM daemon

*/

#ifndef __CLPKM__SCHEDULE_SERVICE_HPP__
#define __CLPKM__SCHEDULE_SERVICE_HPP__

#include "TaskKind.hpp"
#include <condition_variable>
#include <mutex>
#include <thread>
#include <systemd/sd-bus.h>
#include <variant>



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

	const std::string& getCompilerPath() const { return CompilerPath; }

	uint64_t getCRThreshold() const { return Threshold; }
	priority getPriority() const { return Priority; }

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

	// Task management related
	static constexpr size_t NumOfTaskKind = static_cast<size_t>(
			task_kind::NUM_OF_TASK_KIND);

	typedef struct {
		unsigned                Count[NumOfTaskKind] = {};
		std::condition_variable CV;
		} high_prio_task;

	typedef struct {
		std::condition_variable CV[NumOfTaskKind];
		} low_prio_task;

	// Mutex to protect bitmap
	std::mutex  Mutex;
	task_bitmap Bitmap;

	std::variant<low_prio_task, high_prio_task> Task;

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
