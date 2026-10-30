"""The reconciler is the only thing that applies persisted state to the engine.

Nothing else in the API can report its loss: every write path succeeds by touching
only the database, so a stopped or stalled reconciler leaves the service answering
normally while nothing it is told ever takes effect.  These tests pin the two
properties that make that impossible.
"""

import asyncio
import threading
from contextlib import suppress

from transcoder_api import main


def _drive(seconds: float) -> None:
    """Run the reconciler for a bounded window, then cancel it as shutdown does."""

    async def run() -> None:
        task = asyncio.create_task(main._playlist_reconciler())
        await asyncio.sleep(seconds)
        task.cancel()
        # A loop that swallowed CancelledError would hang here rather than fail,
        # so the timeout is the assertion.
        with suppress(asyncio.CancelledError):
            await asyncio.wait_for(task, timeout=5)

    asyncio.run(run())


def test_reconciler_keeps_ticking_after_a_failing_pass(monkeypatch) -> None:
    """A transient failure must not end the loop -- the whole point of F-01."""
    calls: list[int] = []

    def flaky() -> None:
        calls.append(1)
        if len(calls) == 1:
            raise RuntimeError("database is locked")

    monkeypatch.setattr(main, "_reconcile_once", flaky)
    monkeypatch.setattr(main.rundown_service, "enabled", lambda: True)

    _drive(0.9)

    # The first pass raised; anything past it proves the loop outlived the failure.
    assert len(calls) >= 3, f"loop stopped after {len(calls)} pass(es)"


def test_reconciliation_runs_off_the_event_loop(monkeypatch) -> None:
    """The pass is blocking SQLite and gRPC work, so it must not run on the loop."""
    threads: list[str] = []

    monkeypatch.setattr(
        main, "_reconcile_once", lambda: threads.append(threading.current_thread().name)
    )
    monkeypatch.setattr(main.rundown_service, "enabled", lambda: True)

    loop_thread: list[str] = []

    async def run() -> None:
        loop_thread.append(threading.current_thread().name)
        task = asyncio.create_task(main._playlist_reconciler())
        await asyncio.sleep(0.6)
        task.cancel()
        with suppress(asyncio.CancelledError):
            await asyncio.wait_for(task, timeout=5)

    asyncio.run(run())

    assert threads, "reconciler never ran"
    assert all(name != loop_thread[0] for name in threads), (
        f"reconciliation ran on the event loop thread {loop_thread[0]}"
    )
