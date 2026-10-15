# Bounded asynchronous task scheduling

The core task graph is a C++20 standard-library foundation for work such as RMS
waveform generation, storyboard evaluation, rendering, and heavy exports. It uses a
fixed `std::jthread` worker pool and does not expose `std::async`. A graph is built
before submission; dependencies may only name earlier tasks, so the graph is acyclic
and its fan-in order is explicit. Outstanding capacity is clamped to at least one
when zero is supplied. The requested worker count is also at least one. When
`std::thread::hardware_concurrency()` reports `H > 0`, each pool is capped at
`max(1, H - 2)` workers; an unknown count (`H == 0`) does not cap the request.
This is a per-pool limit, not a shared process-wide budget: several pools and any
auxiliary threads can together exceed it. It does not reserve two physical cores.

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

An optional `task_scheduler_options_t::on_worker_start` hook runs once on each
actual worker before it takes tasks. A throwing hook is ignored and the worker
continues; the core scheduler does not depend on SDL or impose an OS policy.
Production application pools pass `background_scheduler_options()`, which requests
SDL low priority. The separate chart-preview refill worker requests high priority,
and the frame thread retains its default priority. OS rejection is nonfatal and
logs at most one warning per process and requested priority. These are best-effort
requests: neither OS acceptance nor absence of frame/audio starvation is guaranteed.

This is a bounded scheduling primitive, not a complete async runtime. It does not
provide per-task priorities, work stealing, deadlines, automatic memory accounting,
or callback preemption. Callers choose worker count and task capacity for the
platform, keep UI thread work synchronous and small, and measure whether parallel
RMS/render workloads help within their interaction and memory budgets. Deterministic
clamp/start-hook tests cover wiring and exception tolerance; actual OS priority
effectiveness and concurrent preload/playback without dropouts still need manual
validation on the target platform.


`async_job_t<Request, Payload, Identity>` owns bounded, latest-only requests on
this scheduler. Its request capture runs synchronously once, after admission, and
returns the worker-owned inputs. A named work function returns a typed payload or
task error; the shell publishes one identity-bearing completion through `poll()`.
It reaps every terminal handle, discards superseded generations, and cancels old
work only after a replacement has submitted successfully. `wait()` is for tests
and shutdown, not the frame loop. Owners retain validation and UI publication.

The default cancellation policy discards the current completion immediately;
advisory owners can explicitly retain it to collect cancellation feedback. Default
teardown cancels and waits, protecting workers that borrow owner state. Owners
whose requests own all their inputs can explicitly release handles on destruction
without cancelling or waiting, preserving nonblocking session reset. Their existing
scheduler shutdown remains
responsible for joining workers. These policies do not change scheduler capacity,
priorities, cooperative stop points, or domain cleanup/publication rules.


Single-flight handle records live inline; larger bounds reserve vector storage at
construction. Lazy single-flight owners construct the shell in place without a
heap allocation for the shell or its handle list. Moves transfer outstanding
handles and generations. Referenced resources must still outlive the receiving
job.

These existing services retain explicit scheduler orchestration and domain policy:

- Playing waveform detail (`waveform_analysis_runtime.cpp`) remains busy after
  cancellation until terminal collection, with its existing null-payload failure handling.
- Published output export (`output_group_export_t`) reports files even if the
  editor document changes after publication.
- Metadata import-copy (`metadata_asset_t`) retains receipts for orphan cleanup
  after the shared copy scheduler joins.
- Autosave (`native_project_autosave_loop_t`) serializes coordinator progress,
  deferred explicit saves, retry and session changes.
- Audition source probes drain a registry queue with fan-out and synchronized
  failure exchange.
- Resident loading (`resident_sample_loader_t`) owns progress, priorities and
  budget retries; it cancels old work before attempting replacement submission.
