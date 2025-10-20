"""
High-level Python wrapper for the nxp_simtemp driver.

Typical usage:

    from apitest.LxDrTemp import SimTempDriver, SimulationMode

    with SimTempDriver() as dev:
        dev.set_simulation_mode(SimulationMode.RAMP)
        dev.set_sampling_period_ms(200)
        dev.start()
        sample = dev.read_sample()
        print(sample.temp_c)
        dev.stop()

The class primarily uses ioctls on `/dev/nxp_simtemp` for control (start/stop,
operation mode, period, threshold) so non-root users can interact as long as
the device node is accessible. Sysfs is still leveraged for read-only data and
for simulation-mode changes.
"""

from __future__ import annotations

import enum
import errno
import fcntl
import os
import select
import struct
from dataclasses import dataclass
from pathlib import Path
from array import array
from typing import List, Optional, Union

# Flags replicated from include/uapi/simtemp_uapi.h for convenience.
SIMTEMP_FLAG_OK = 1 << 0
SIMTEMP_FLAG_ONESHOT_DONE = 1 << 3
SIMTEMP_FLAG_OVERFLOW = 1 << 16
SIMTEMP_FLAG_THR_EDGE = 1 << 17

_SAMPLE_STRUCT = struct.Struct("<QiI")

_IOC_NRBITS = 8
_IOC_TYPEBITS = 8
_IOC_SIZEBITS = 14
_IOC_DIRBITS = 2

_IOC_NRSHIFT = 0
_IOC_TYPESHIFT = _IOC_NRSHIFT + _IOC_NRBITS
_IOC_SIZESHIFT = _IOC_TYPESHIFT + _IOC_TYPEBITS
_IOC_DIRSHIFT = _IOC_SIZESHIFT + _IOC_SIZEBITS

_IOC_NONE = 0
_IOC_WRITE = 1
_IOC_READ = 2


def _ioc(direction: int, ioc_type: int, number: int, size: int) -> int:
    return (
        (direction << _IOC_DIRSHIFT)
        | (ioc_type << _IOC_TYPESHIFT)
        | (number << _IOC_NRSHIFT)
        | (size << _IOC_SIZESHIFT)
    )


def _io(ioc_type: int, number: int) -> int:
    return _ioc(_IOC_NONE, ioc_type, number, 0)


def _ior(ioc_type: int, number: int, size: int) -> int:
    return _ioc(_IOC_READ, ioc_type, number, size)


def _iow(ioc_type: int, number: int, size: int) -> int:
    return _ioc(_IOC_WRITE, ioc_type, number, size)


SIMTEMP_IOC_MAGIC = ord("T")
_U32_SIZE = struct.calcsize("I")
_S32_SIZE = struct.calcsize("i")

SIMTEMP_IOC_START = _io(SIMTEMP_IOC_MAGIC, 0x01)
SIMTEMP_IOC_STOP = _io(SIMTEMP_IOC_MAGIC, 0x02)
SIMTEMP_IOC_GET_MODE = _ior(SIMTEMP_IOC_MAGIC, 0x10, _U32_SIZE)
SIMTEMP_IOC_SET_MODE = _iow(SIMTEMP_IOC_MAGIC, 0x11, _U32_SIZE)
SIMTEMP_IOC_GET_PERIOD = _ior(SIMTEMP_IOC_MAGIC, 0x20, _U32_SIZE)
SIMTEMP_IOC_SET_PERIOD = _iow(SIMTEMP_IOC_MAGIC, 0x21, _U32_SIZE)
SIMTEMP_IOC_GET_THRESHOLD = _ior(SIMTEMP_IOC_MAGIC, 0x30, _S32_SIZE)
SIMTEMP_IOC_SET_THRESHOLD = _iow(SIMTEMP_IOC_MAGIC, 0x31, _S32_SIZE)


class SimTempError(Exception):
    """Base exception for SimTemp driver helper."""


class SimTempTimeoutError(SimTempError, TimeoutError):
    """Raised when waiting for samples times out."""


class SimTempNotAvailableError(SimTempError, FileNotFoundError):
    """Raised when the driver or its sysfs nodes are missing."""


class DriverState(enum.Enum):
    STOP = 0
    RUN = 1


class OperationMode(str, enum.Enum):
    CONTINUOUS = "continuous"
    ONE_SHOT = "one-shot"


class SimulationMode(str, enum.Enum):
    NORMAL = "normal"
    NOISY = "noisy"
    RAMP = "ramp"


@dataclass(frozen=True)
class SimTempSample:
    timestamp_ns: int
    temp_mC: int
    flags: int

    @property
    def temp_c(self) -> float:
        return self.temp_mC / 1000.0

    def has_flag(self, flag: int) -> bool:
        return bool(self.flags & flag)


@dataclass(frozen=True)
class SimTempStats:
    samples: int
    overruns: int
    alerts: int
    alert_pending: bool
    overflow_pending: bool
    threshold_mC: int


class SimTempDriver:
    """Convenience wrapper around nxp_simtemp sysfs and character device."""

    def __init__(
        self,
        device_path: Union[Path, str] = "/dev/nxp_simtemp",
        sysfs_base: Union[Path, str] = "/sys/class/misc/nxp_simtemp",
        auto_open: bool = False,
    ) -> None:
        self.device_path = Path(device_path)
        self.sysfs_base = Path(sysfs_base)
        self._fd: Optional[int] = None
        self._read_only: bool = False
        if auto_open:
            self.open()

    def __enter__(self) -> "SimTempDriver":
        self.open()
        return self

    def __exit__(self, exc_type, exc_value, traceback) -> None:
        self.close()

    # -- Device file management -------------------------------------------------

    @property
    def is_open(self) -> bool:
        return self._fd is not None

    def open(self) -> None:
        if self.is_open:
            return
        if not self.device_path.exists():
            raise SimTempNotAvailableError(f"Device node {self.device_path} not found")
        # Use O_RDWR when possible to allow ioctl-based configuration.
        try:
            flags = os.O_RDWR | os.O_CLOEXEC
            self._fd = os.open(self.device_path, flags)
            self._read_only = False
        except PermissionError:
            # Fall back to read-only access; configuration methods will fail.
            self._fd = os.open(self.device_path, os.O_RDONLY | os.O_CLOEXEC)
            self._read_only = True

    def close(self) -> None:
        if self._fd is not None:
            os.close(self._fd)
            self._fd = None
            self._read_only = False

    def fileno(self) -> int:
        self._ensure_open()
        assert self._fd is not None
        return self._fd

    # -- Public helpers ---------------------------------------------------------

    def start(self) -> None:
        self._ioctl_noarg(SIMTEMP_IOC_START)

    def stop(self) -> None:
        self._ioctl_noarg(SIMTEMP_IOC_STOP)

    def get_state(self) -> DriverState:
        raw = self._read_sysfs("state")
        try:
            value = int(raw)
        except ValueError as exc:
            raise SimTempError(f"Unexpected state value: {raw!r}") from exc
        try:
            return DriverState(value)
        except ValueError as exc:
            raise SimTempError(f"Unknown state code: {value}") from exc

    def set_operation_mode(self, mode: Union[OperationMode, str]) -> None:
        mode_value = OperationMode(mode) if not isinstance(mode, OperationMode) else mode
        self._ioctl_set_u32(SIMTEMP_IOC_SET_MODE, 0 if mode_value is OperationMode.ONE_SHOT else 1)

    def get_operation_mode(self) -> OperationMode:
        raw = self._ioctl_get_u32(SIMTEMP_IOC_GET_MODE)
        if raw == 0:
            return OperationMode.ONE_SHOT
        if raw == 1:
            return OperationMode.CONTINUOUS
        raise SimTempError(f"Unknown operation mode code: {raw}")

    def set_simulation_mode(self, mode: Union[SimulationMode, str]) -> None:
        mode_value = SimulationMode(mode) if not isinstance(mode, SimulationMode) else mode
        self._write_sysfs("mode", mode_value.value)

    def get_simulation_mode(self) -> SimulationMode:
        value = self._read_sysfs("mode").strip()
        try:
            return SimulationMode(value)
        except ValueError as exc:
            raise SimTempError(f"Unknown simulation mode: {value}") from exc

    def set_sampling_period_ms(self, period_ms: int) -> None:
        if period_ms <= 0:
            raise ValueError("period_ms must be positive")
        self._ioctl_set_u32(SIMTEMP_IOC_SET_PERIOD, int(period_ms))

    def get_sampling_period_ms(self) -> int:
        return int(self._ioctl_get_u32(SIMTEMP_IOC_GET_PERIOD))

    def set_threshold_mc(self, threshold: int) -> None:
        self._ioctl_set_s32(SIMTEMP_IOC_SET_THRESHOLD, int(threshold))

    def get_threshold_mc(self) -> int:
        return int(self._ioctl_get_s32(SIMTEMP_IOC_GET_THRESHOLD))

    def read_stats(self) -> SimTempStats:
        raw = self._read_sysfs("stats").strip()
        data = {}
        for entry in raw.split():
            if "=" not in entry:
                continue
            key, value = entry.split("=", 1)
            data[key] = value
        try:
            return SimTempStats(
                samples=int(data["samples"]),
                overruns=int(data["overruns"]),
                alerts=int(data["alerts"]),
                alert_pending=bool(int(data["alert_pending"])),
                overflow_pending=bool(int(data["overflow_pending"])),
                threshold_mC=int(data["threshold_mC"]),
            )
        except KeyError as exc:
            raise SimTempError(f"Incomplete stats payload: {raw!r}") from exc

    def read_sample(self, timeout: Optional[float] = None) -> SimTempSample:
        samples = self.read_samples(1, timeout=timeout)
        if not samples:
            raise SimTempError("No sample returned")
        return samples[0]

    def read_samples(self, count: int, timeout: Optional[float] = None) -> List[SimTempSample]:
        if count <= 0:
            return []
        self._ensure_open()
        assert self._fd is not None
        total_bytes = _SAMPLE_STRUCT.size * count

        if timeout is not None:
            readable, _, _ = select.select([self._fd], [], [], timeout)
            if not readable:
                raise SimTempTimeoutError(f"Timeout waiting for {count} sample(s)")

        buffer = bytearray()
        while len(buffer) < total_bytes:
            chunk = os.read(self._fd, total_bytes - len(buffer))
            if not chunk:
                raise SimTempError("Device returned EOF while reading samples")
            buffer.extend(chunk)

        return [
            SimTempSample(*values)
            for values in _SAMPLE_STRUCT.iter_unpack(buffer)
        ]

    # -- Internals --------------------------------------------------------------

    def _ensure_open(self) -> None:
        if not self.is_open:
            self.open()

    def _sysfs_path(self, name: str) -> Path:
        path = self.sysfs_base / name
        if not path.exists():
            raise SimTempNotAvailableError(f"Sysfs entry {path} not found")
        return path

    def _read_sysfs(self, name: str) -> str:
        path = self._sysfs_path(name)
        return path.read_text(encoding="ascii").strip()

    def _write_sysfs(self, name: str, value: str) -> None:
        path = self._sysfs_path(name)
        text = value if value.endswith("\n") else f"{value}\n"
        path.write_text(text, encoding="ascii")

    def _ioctl_noarg(self, cmd: int) -> None:
        self._require_write_access()
        fd = self.fileno()
        try:
            fcntl.ioctl(fd, cmd)
        except OSError as exc:
            raise SimTempError(self._format_ioctl_error(cmd, exc)) from exc

    def _ioctl_get_u32(self, cmd: int) -> int:
        buf = array("I", [0])
        fd = self.fileno()
        try:
            fcntl.ioctl(fd, cmd, buf, True)
        except OSError as exc:
            raise SimTempError(self._format_ioctl_error(cmd, exc)) from exc
        return buf[0]

    def _ioctl_set_u32(self, cmd: int, value: int) -> None:
        self._require_write_access()
        buf = array("I", [value & 0xFFFFFFFF])
        fd = self.fileno()
        try:
            fcntl.ioctl(fd, cmd, buf)
        except OSError as exc:
            raise SimTempError(self._format_ioctl_error(cmd, exc)) from exc

    def _ioctl_get_s32(self, cmd: int) -> int:
        buf = array("i", [0])
        fd = self.fileno()
        try:
            fcntl.ioctl(fd, cmd, buf, True)
        except OSError as exc:
            raise SimTempError(self._format_ioctl_error(cmd, exc)) from exc
        return buf[0]

    def _ioctl_set_s32(self, cmd: int, value: int) -> None:
        self._require_write_access()
        buf = array("i", [int(value)])
        fd = self.fileno()
        try:
            fcntl.ioctl(fd, cmd, buf)
        except OSError as exc:
            raise SimTempError(self._format_ioctl_error(cmd, exc)) from exc

    def _format_ioctl_error(self, cmd: int, exc: OSError) -> str:
        if exc.errno in (errno.EPERM, errno.EACCES, errno.EBADF):
            return (
                f"ioctl 0x{cmd:08x} requires write access; adjust device permissions "
                "or open with sufficient rights."
            )
        return f"ioctl 0x{cmd:08x} failed: {exc.strerror}"

    def _require_write_access(self) -> None:
        if self._read_only:
            raise SimTempError(
                "Device is opened read-only; reopen with write access or adjust permissions."
            )
