# Bounded asynchronous task scheduling

The core task graph is a C++20 standard-library foundation for work such as RMS
waveform generation, storyboard evaluation, rendering, and heavy exports. It uses a
fixed `std::jthread` worker pool and does not expose `std::async`. A graph is built
before submission; dependencies may only name earlier tasks, so the graph is acyclic
and its fan-in order is explicit. Worker count and outstanding capacity are clamped to
at least one when zero is supplied.

The scheduler has a fixed ring queue and a maximum outstanding-node count. That count
includes tasks that are waiting on dependencies, queued, or running across all graphs.
Submission beyond the bound returns a capacity error. The bound covers scheduler
bookkeeping and queue entries, not memory that a callback allocates or keeps in its
result. Results use immutable, runtime-type-checked shared payloads so fan-in can pass
large buffers without copying them; a mismatched payload cast returns empty.

Callbacks return a success value or a typed error. The worker boundary catches thrown
exceptions and converts them to `task_threw`; exceptions do not escape into worker
threads. A failed dependency prevents its descendants from running and reports
`dependency_failed`. Graph cancellation stops queued and dependency-blocked work and
requests a stop token from active callbacks. Shutdown does the same for every graph
and joins all workers.

Cancellation is cooperative: callbacks must check their `std::stop_token` and return
promptly. A callback blocked in non-interruptible I/O can delay shutdown. The
generation guard serializes a short publication action against generation changes;
use it for actions such as swapping a completed frame or export result into the active
document. It does not cancel obsolete work by itself, and its publication callback
must not call `begin_generation()` recursively.

Callbacks must not synchronously wait on a graph handle or destroy the scheduler
that is executing them. Dependencies express continuation order; scheduler lifetime
belongs to the owning application or service thread.

This is a bounded scheduling primitive, not a complete async runtime. It does not
provide priorities, work stealing, deadlines, automatic memory accounting, or
preemption. Callers choose worker count and task capacity for the platform, keep UI
thread work synchronous and small, and measure whether parallel RMS/render workloads
help within their interaction and memory budgets.
