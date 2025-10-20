"""
Example script that performs a one-shot read from the nxp_simtemp driver.

Requires the driver to be loaded and the current user to have read/write access
to `/dev/nxp_simtemp` (e.g. via udev rule or by running with sudo).
"""

from __future__ import annotations

import sys
from typing import NoReturn

from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[1]
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from apitest.LxDrTemp import (
    OperationMode,
    SIMTEMP_FLAG_ONESHOT_DONE,
    SimTempDriver,
    SimTempError,
    SimTempTimeoutError,
)


def oneshot_example() -> None:
    """Configure the driver in one-shot mode and print a single sample."""
    with SimTempDriver(auto_open=True) as dev:
        # Ensure we start from a known state.
        dev.stop()
        dev.set_operation_mode(OperationMode.ONE_SHOT)

        # Trigger the one-shot capture.
        dev.start()
        sample = dev.read_sample(timeout=1.0)

        # The driver automatically returns to STOP after the one-shot.
        print(f"timestamp={sample.timestamp_ns} ns")
        print(f"temperature={sample.temp_c:.3f} °C ({sample.temp_mC} mC)")
        print(f"flags=0x{sample.flags:08x}")
        if sample.has_flag(SIMTEMP_FLAG_ONESHOT_DONE):
            print("one-shot complete flag set")


def main() -> NoReturn:
    try:
        oneshot_example()
    except SimTempTimeoutError:
        print("Timed out waiting for the one-shot sample.", file=sys.stderr)
        sys.exit(1)
    except SimTempError as exc:
        print(f"SimTemp error: {exc}", file=sys.stderr)
        sys.exit(1)
    except OSError as exc:
        print(f"OS error: {exc}", file=sys.stderr)
        sys.exit(1)
    else:
        sys.exit(0)


if __name__ == "__main__":
    main()
