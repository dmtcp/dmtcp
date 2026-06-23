#!/usr/bin/env python3

import argparse
import concurrent.futures
from collections import Counter
import errno
import json
import os
import pathlib
import pty
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time
import traceback
from dataclasses import dataclass, field, replace
from random import sample
from typing import Callable, Dict, Iterable, List, Optional, Tuple, Union

import autotest_config


ROOT = pathlib.Path(__file__).resolve().parents[1]
SLOW_FACTOR_BASE = 5
DEFAULT_SLOW_PRE_CHECKPOINT_DELAY = 0.3
VALID_LAUNCH_MODES = frozenset({"pipe", "pty"})
COLOR_RED = "\033[0;31m"
COLOR_RESET = "\033[0m"
COLOR_MODE = "auto"
RUN_TESTNAME_WIDTH = 15
COMMAND_OUTPUT_TAIL_LINES = 20
ARTIFACT_LOG_HINT = (
    "result.log commands.log processes.log worker-*.out restart-*.out"
)
COORDINATOR_PROTOCOL_CATEGORY = "Coordinator protocol tests"


class DmtcpCommandJson:
    SCHEMA_VERSION = 1
    SUCCESS_STATUS = "DMT_COORD_SUCCESS"

    @classmethod
    def validate_payload(cls, payload: object):
        if not isinstance(payload, dict):
            raise ValueError("dmtcp_command JSON payload must be an object")
        version = payload.get("schema_version")
        if version != cls.SCHEMA_VERSION:
            raise ValueError(
                f"unsupported dmtcp_command JSON schema: {version!r}"
            )

    @classmethod
    def parse(cls, raw_output: str) -> Dict[str, object]:
        try:
            payload = json.loads(raw_output)
        except json.JSONDecodeError as error:
            if ("{" in raw_output and "}" in raw_output) or (
                    "[" in raw_output and "]" in raw_output):
                raise ValueError(f"mixed human/JSON output: {error}") from error
            raise ValueError(f"invalid JSON output: {error}") from error
        cls.validate_payload(payload)
        return payload

    @classmethod
    def validate_result(cls, payload: Dict[str, object],
                        expected_command: str):
        cls.validate_payload(payload)
        if "type" in payload:
            raise ValueError(
                "dmtcp_command JSON field type has been replaced by command")
        actual_command = payload.get("command")
        if actual_command != expected_command:
            raise ValueError(
                f"expected JSON command '{expected_command}', "
                f"got {actual_command!r}"
            )
        if "phase" in payload:
            raise ValueError(
                "dmtcp_command JSON field phase is redundant")
        if "ok" in payload:
            raise ValueError("dmtcp_command JSON field ok is obsolete")
        command_status = payload.get("command_status")
        if type(command_status) is not str:
            raise ValueError(
                "dmtcp_command JSON field command_status must be a string")
        if "coordinator_host" in payload:
            coordinator_host = payload.get("coordinator_host")
            if type(coordinator_host) is not str:
                raise ValueError(
                    "dmtcp_command JSON field coordinator_host must be a string")
        if "coordinator_port" in payload:
            coordinator_port = payload.get("coordinator_port")
            if type(coordinator_port) is not int:
                raise ValueError(
                    "dmtcp_command JSON field coordinator_port must be an "
                "integer")

    @classmethod
    def is_success(cls, payload: Dict[str, object]) -> bool:
        return payload.get("command_status") == cls.SUCCESS_STATUS


def dmtcp_command_json_command(command: str) -> str:
    return {
        "--status": "DMT_STATUS",
        "--list": "DMT_LIST",
        "--checkpoint": "DMT_CHECKPOINT",
        "--bcheckpoint": "DMT_BLOCKING_CKPT",
        "-bc": "DMT_BLOCKING_CKPT",
        "--kcheckpoint": "DMT_KILL_AFTER_CKPT",
        "-kc": "DMT_KILL_AFTER_CKPT",
        "--interval": "DMT_UPDATE_CKPT_INTERVAL",
        "-i": "DMT_UPDATE_CKPT_INTERVAL",
        "--kill": "DMT_KILL",
        "--quit": "DMT_QUIT",
        "--help": "DMT_HELP",
    }.get(command, "DMT_INVALID_COORDINATOR_COMMAND")


class HarnessFailure(Exception):
    def __init__(self, phase: str, message: str):
        super().__init__(message)
        self.phase = phase
        self.message = message


@dataclass(frozen=True)
class DmtcpStatus:
    num_peers: int
    running: bool
    checkpoint_interval: int

    @staticmethod
    def from_json(payload: Dict[str, object]) -> "DmtcpStatus":
        DmtcpCommandJson.validate_payload(payload)
        if payload.get("command") != "DMT_STATUS" or \
                not DmtcpCommandJson.is_success(payload):
            raise ValueError("JSON payload is not a successful status result")
        for field_name in ("num_peers", "running", "checkpoint_interval"):
            if field_name not in payload:
                raise ValueError(f"missing status JSON field: {field_name}")
        num_peers = payload["num_peers"]
        running = payload["running"]
        checkpoint_interval = payload["checkpoint_interval"]
        if type(num_peers) is not int:
            raise ValueError("status JSON field num_peers must be an integer")
        if type(running) is not bool:
            raise ValueError("status JSON field running must be a boolean")
        if type(checkpoint_interval) is not int:
            raise ValueError(
                "status JSON field checkpoint_interval must be an integer")
        return DmtcpStatus(
            num_peers=num_peers,
            running=running,
            checkpoint_interval=checkpoint_interval,
        )


@dataclass(frozen=True)
class TestResult:
    name: str
    passed: bool
    phase: str
    message: str
    artifact_dir: Optional[pathlib.Path] = None
    details: str = ""

    @staticmethod
    def pass_(name: str, artifact_dir: Optional[pathlib.Path] = None,
              message: str = "", details: str = "") -> "TestResult":
        return TestResult(name, True, "complete", message, artifact_dir,
                          details)

    @staticmethod
    def fail(name: str, phase: str, message: str,
             artifact_dir: Optional[pathlib.Path] = None,
             details: str = "") -> "TestResult":
        return TestResult(name, False, phase, message, artifact_dir, details)


@dataclass(frozen=True)
class TestSpec:
    name: str
    peers: Union[int, List[int]]
    commands: List[str]
    cycles: int = 2
    timeout: float = 30.0
    checkpoint_command: str = "--checkpoint"
    pre_checkpoint_delay: float = 0.0
    post_launch_delay: float = 0.0
    env: Dict[str, Optional[str]] = field(default_factory=dict)
    library_paths: List[str] = field(default_factory=list)
    restart_uses_directory: bool = False
    restart_args: List[str] = field(default_factory=list)
    validate_checkpoint_headers: bool = False
    expect_checkpoint_gzip: Optional[bool] = None
    checkpoint_dir_files: Dict[str, str] = field(default_factory=dict)
    completion_command: str = "--kill"
    coordinator_args: List[str] = field(default_factory=list)
    category: str = "Single-process programs"
    tags: List[str] = field(default_factory=list)
    requirements: List[str] = field(default_factory=list)
    configure_flags: List[str] = field(default_factory=list)
    blocked_configure_flags: List[str] = field(default_factory=list)
    required_files: List[str] = field(default_factory=list)
    limits: List[str] = field(default_factory=list)
    list_notes: List[str] = field(default_factory=list)
    launch_mode: str = "pipe"
    private_env_dirs: Dict[str, str] = field(default_factory=dict)
    replace_worker_index: Optional[int] = None
    reject_restart_while_running: bool = False
    restart_pause_level: Optional[int] = None
    run_serial: bool = False
    post_restart_validator: Optional[Callable[["TestContext"], None]] = None
    post_run_validator: Optional[Callable[["TestContext"], None]] = None

    def peer_counts(self) -> List[int]:
        if isinstance(self.peers, int):
            return [self.peers]
        return list(self.peers)

    def checkpoint_kills_workers(self) -> bool:
        return self.checkpoint_command in ["--kcheckpoint", "-kc"]


@dataclass(frozen=True)
class CommandTestSpec:
    name: str
    category: str
    suite: str
    command: List[str]
    cwd: pathlib.Path = ROOT / "test"


@dataclass(frozen=True)
class FailureRecord:
    name: str
    phase: str
    message: str
    artifact_dir: Optional[pathlib.Path] = None
    command: Optional[List[str]] = None
    cwd: Optional[pathlib.Path] = None


class PtyProcess:
    def __init__(self, pid: int, master_fd: int, output_path: pathlib.Path):
        self.pid = pid
        self.returncode: Optional[int] = None
        self._master_fd: Optional[int] = master_fd
        self._fd_lock = threading.Lock()
        self._reader = threading.Thread(
            target=self._drain_master,
            args=(output_path,),
            daemon=True,
        )
        self._reader.start()

    @staticmethod
    def _returncode_from_status(status: int) -> int:
        if os.WIFEXITED(status):
            return os.WEXITSTATUS(status)
        if os.WIFSIGNALED(status):
            return -os.WTERMSIG(status)
        return status

    def _close_master(self):
        with self._fd_lock:
            master_fd = self._master_fd
            self._master_fd = None
        if master_fd is not None:
            try:
                os.close(master_fd)
            except OSError:
                pass

    def _drain_master(self, output_path: pathlib.Path):
        with output_path.open("ab", buffering=0) as out:
            while True:
                with self._fd_lock:
                    master_fd = self._master_fd
                if master_fd is None:
                    break
                try:
                    data = os.read(master_fd, 4096)
                except OSError as error:
                    if error.errno in (errno.EBADF, errno.EIO):
                        break
                    raise
                if not data:
                    break
                out.write(data)
        self._close_master()

    def _finish(self, returncode: int) -> int:
        self.returncode = returncode
        self._close_master()
        if threading.current_thread() is not self._reader:
            self._reader.join(timeout=1)
        return returncode

    def poll(self) -> Optional[int]:
        if self.returncode is not None:
            return self.returncode
        try:
            waited_pid, status = os.waitpid(self.pid, os.WNOHANG)
        except ChildProcessError:
            return self._finish(0)
        if waited_pid == 0:
            return None
        return self._finish(self._returncode_from_status(status))

    def wait(self, timeout: Optional[float] = None) -> int:
        if timeout is None:
            while True:
                returncode = self.poll()
                if returncode is not None:
                    return returncode
                time.sleep(0.05)

        deadline = time.time() + timeout
        while time.time() < deadline:
            returncode = self.poll()
            if returncode is not None:
                return returncode
            time.sleep(0.05)
        raise subprocess.TimeoutExpired(self.pid, timeout)


class DmtcpHarness:
    def __init__(self, root: pathlib.Path = ROOT, verbose: bool = False,
                 retain_success_artifacts: bool = False,
                 slow_count: int = 0):
        self.root = root
        self.verbose = verbose
        self.retain_success_artifacts = retain_success_artifacts
        self.slow_count = slow_count
        self.slow_factor = SLOW_FACTOR_BASE ** slow_count
        self.bin_dir = self.root / "bin"
        self.coordinator = self.bin_dir / "dmtcp_coordinator"
        self.command = self.bin_dir / "dmtcp_command"
        self.launch = self.bin_dir / "dmtcp_launch"
        self.restart = self.bin_dir / "dmtcp_restart"
        self.progress = lambda event: None

    def run(self, spec: TestSpec) -> TestResult:
        spec = self._scaled_spec(spec)
        work = TestWorkDir(self.root, spec.name)
        context = TestContext(self, spec, work)
        result = None
        try:
            context.run()
            artifact_dir = work.path if self.retain_success_artifacts else None
            result = TestResult.pass_(spec.name, artifact_dir)
        except HarnessFailure as failure:
            result = TestResult.fail(spec.name, failure.phase, failure.message,
                                     work.path)
        except Exception as failure:
            traceback_path = work.path / "harness-error.log"
            traceback_path.write_text(traceback.format_exc(),
                                      encoding="utf-8")
            result = TestResult.fail(spec.name, "harness",
                                     str(failure), work.path)
        finally:
            self._record_process_status(context, "before-cleanup")
            try:
                context.cleanup()
            except Exception as failure:
                traceback_path = work.path / "cleanup-error.log"
                traceback_path.write_text(traceback.format_exc(),
                                          encoding="utf-8")
                if result is None or result.passed:
                    result = TestResult.fail(spec.name, "cleanup",
                                             str(failure), work.path)
            self._record_process_status(context, "after-cleanup")
            if result is not None and result.artifact_dir is not None:
                self._record_result(result)
            if result is not None and result.passed and not self.retain_success_artifacts:
                work.cleanup()
        return result

    def _scaled_spec(self, spec: TestSpec) -> TestSpec:
        if self.slow_count == 0:
            return spec

        pre_checkpoint_delay = spec.pre_checkpoint_delay
        if pre_checkpoint_delay == 0.0:
            pre_checkpoint_delay = DEFAULT_SLOW_PRE_CHECKPOINT_DELAY

        return replace(
            spec,
            timeout=spec.timeout * self.slow_factor,
            pre_checkpoint_delay=pre_checkpoint_delay * self.slow_factor,
        )

    def _record_result(self, result: TestResult):
        with (result.artifact_dir / "result.log").open(
                "w", encoding="utf-8") as out:
            out.write(f"name={result.name}\n")
            out.write(f"passed={result.passed}\n")
            out.write(f"phase={result.phase}\n")
            out.write(f"message={result.message}\n")

    def _record_process_status(self, context: "TestContext", phase: str):
        try:
            context._record_process_status(phase)
        except Exception:
            with (context.work.path / "process-status-error.log").open(
                    "a", encoding="utf-8") as out:
                out.write(f"phase={phase}\n")
                out.write(traceback.format_exc())


class TestWorkDir:
    def __init__(self, root: pathlib.Path, test_name: str):
        self.path = pathlib.Path(tempfile.mkdtemp(prefix=f"dmtcp-{test_name}-",
                                                  dir=str(root)))
        self.ckpt_dir = self.path / "ckpt"
        self.ckpt_dir.mkdir()
        self.port_file = self.path / "coordinator.port"

    def cleanup(self):
        shutil.rmtree(self.path, ignore_errors=True)


class TestContext:
    def __init__(self, harness: DmtcpHarness, spec: TestSpec,
                 work: TestWorkDir):
        self.harness = harness
        self.spec = spec
        self.work = work
        self.private_env_paths: List[pathlib.Path] = []
        self._created_private_env_paths: List[pathlib.Path] = []
        self.env = self._make_env()
        self.coordinator_proc: Optional[subprocess.Popen] = None
        self.processes: List[subprocess.Popen] = []
        self.port: Optional[int] = None

    def run(self):
        self._verify_spec()
        self._verify_binaries()
        self._start_coordinator()
        self._assert_status(0, False, "initial-status")
        self._launch_processes()
        self._wait_for_status(self.spec.peer_counts(), True, "launch")
        if self.spec.replace_worker_index is not None:
            self._replace_worker(self.spec.replace_worker_index)
        if self.spec.post_launch_delay > 0.0:
            time.sleep(self.spec.post_launch_delay)
        if self.spec.reject_restart_while_running:
            self.harness.progress("ckpt-start")
            self._checkpoint()
            self.harness.progress("ckpt-passed")
            self.harness.progress("rstr-start")
            self._assert_restart_rejected_while_running()
            self.harness.progress("rstr-passed")
        else:
            for cycle in range(self.spec.cycles):
                if cycle:
                    self.harness.progress("cycle-separator")
                self.harness.progress("ckpt-start")
                self._checkpoint()
                self.harness.progress("ckpt-passed")
                self._kill_workers()
                self.harness.progress("rstr-start")
                self._restart()
                self.harness.progress("rstr-passed")
        self._complete_test()
        if self.spec.post_run_validator is not None:
            self.spec.post_run_validator(self)
        if self.spec.cycles == 0 and not self.spec.reject_restart_while_running:
            self.harness.progress("run-passed")

    def cleanup(self):
        self._kill_workers(best_effort=True)
        self._quit_coordinator()
        for proc in self.processes:
            self._terminate_process_group(proc, f"worker {proc.pid}")
        if self.coordinator_proc is not None and self.coordinator_proc.poll() is None:
            self._terminate_process_group(self.coordinator_proc,
                                          f"coordinator {self.coordinator_proc.pid}")
        for path in self._created_private_env_paths:
            shutil.rmtree(path, ignore_errors=True)

    def _verify_spec(self):
        if self.spec.launch_mode not in VALID_LAUNCH_MODES:
            raise HarnessFailure(
                "setup",
                f"unknown launch mode: {self.spec.launch_mode}",
            )

    def _make_env(self) -> Dict[str, str]:
        env = os.environ.copy()
        env["DMTCP_COORD_HOST"] = "localhost"
        env["DMTCP_CHECKPOINT_DIR"] = str(self.work.ckpt_dir)
        env["DMTCP_GZIP"] = env.get("DMTCP_GZIP", "1")
        if not self.harness.verbose:
            env["JALIB_STDERR_PATH"] = os.devnull
        lib_path = str(self.harness.root / "lib")
        if env.get("LD_LIBRARY_PATH"):
            env["LD_LIBRARY_PATH"] = f"{env['LD_LIBRARY_PATH']}:{lib_path}"
        else:
            env["LD_LIBRARY_PATH"] = lib_path
        env.pop("DMTCP_SIGCKPT", None)
        env.pop("MTCP_SIGCKPT", None)
        for name, value in self.spec.env.items():
            if value is None:
                env.pop(name, None)
            else:
                env[name] = value.replace("{workdir}", str(self.work.path))
        for name, path in self.spec.private_env_dirs.items():
            env_dir_template = (path.replace("{workdir}", str(self.work.path))
                                .replace("{workname}", self.work.path.name))
            env_dir = pathlib.Path(env_dir_template)
            if not env_dir.is_absolute():
                env_dir = self.work.path / env_dir
            if env_dir.is_symlink():
                raise HarnessFailure(
                    "setup",
                    f"private env path is a symlink: {env_dir}",
                )
            if env_dir.exists():
                raise HarnessFailure(
                    "setup",
                    f"private env path already exists: {env_dir}",
                )
            env_dir.mkdir(parents=True, exist_ok=False)
            env_dir.chmod(0o700)
            self.private_env_paths.append(env_dir)
            self._created_private_env_paths.append(env_dir)
            env[name] = str(env_dir)
        for path in self.spec.library_paths:
            if env.get("LD_LIBRARY_PATH"):
                env["LD_LIBRARY_PATH"] = f"{env['LD_LIBRARY_PATH']}:{path}"
            else:
                env["LD_LIBRARY_PATH"] = path
        return env

    def _verify_binaries(self):
        for path in [self.harness.coordinator, self.harness.command,
                     self.harness.launch, self.harness.restart]:
            if not path.exists():
                raise HarnessFailure("setup", f"missing binary: {path}")

    def _start_coordinator(self):
        stdout = open(self.work.path / "coordinator.out", "w", encoding="utf-8")
        stderr = open(self.work.path / "coordinator.err", "w", encoding="utf-8")
        coordinator_args = [
            str(self.harness.coordinator),
            "--quiet",
            "--coord-port",
            "0",
            "--port-file",
            str(self.work.port_file),
            "--timeout",
            "10800",
            *self.spec.coordinator_args,
        ]
        self._record_command("start-coordinator", coordinator_args)
        self.coordinator_proc = subprocess.Popen(
            coordinator_args,
            cwd=str(self.harness.root),
            env=self.env,
            text=True,
            stdout=stdout,
            stderr=stderr,
            preexec_fn=os.setpgrp,
        )
        stdout.close()
        stderr.close()
        self.port = self._read_port_file()
        self.env["DMTCP_COORD_PORT"] = str(self.port)

    def _read_port_file(self) -> int:
        deadline = time.time() + 10
        while time.time() < deadline:
            if self.work.port_file.exists():
                data = self.work.port_file.read_text(encoding="utf-8").strip()
                if data:
                    return int(data)
            if self.coordinator_proc is not None and self.coordinator_proc.poll() is not None:
                raise HarnessFailure("coordinator", "coordinator exited before writing port")
            time.sleep(0.05)
        raise HarnessFailure("coordinator", "coordinator did not write port file")

    def _launch_processes(self):
        for index, command in enumerate(self.spec.commands):
            self._launch_process(index, command, f"launch-worker-{index}")

    def _expand_command(self, command: str) -> str:
        return command.replace("{workdir}", shlex.quote(str(self.work.path)))

    def _expand_arg(self, arg: str) -> str:
        return arg.replace("{workdir}", str(self.work.path))

    def _launch_process(self, index: int, command: str, phase: str):
        command = self._expand_command(command)
        command_argv = shlex.split(command)
        if command_argv and command_argv[0].startswith("./test/"):
            executable = self.harness.root / command_argv[0]
            if not executable.exists():
                raise HarnessFailure("setup",
                                     f"missing test binary: {command_argv[0]}")
        argv = [str(self.harness.launch), *command_argv]
        self._record_command(phase, argv)
        proc = self._spawn_worker_process(
            argv,
            self.work.path / f"worker-{index}.out",
            "a",
            phase,
        )
        self.processes.append(proc)

    def _spawn_worker_process(self, argv: List[str], output_path: pathlib.Path,
                              output_mode: str, phase: str):
        if self.spec.launch_mode == "pipe":
            return self._spawn_pipe_process(argv, output_path, output_mode)
        if self.spec.launch_mode == "pty":
            return self._spawn_pty_process(argv, output_path, phase)
        raise HarnessFailure("setup",
                             f"unknown launch mode: {self.spec.launch_mode}")

    def _spawn_pipe_process(self, argv: List[str], output_path: pathlib.Path,
                            output_mode: str):
        stdout = open(output_path, output_mode, encoding="utf-8")
        try:
            return subprocess.Popen(
                argv,
                cwd=str(self.harness.root),
                env=self.env,
                text=True,
                stdin=subprocess.PIPE,
                stdout=stdout,
                stderr=subprocess.STDOUT,
                preexec_fn=os.setpgrp,
            )
        finally:
            stdout.close()

    def _spawn_pty_process(self, argv: List[str], output_path: pathlib.Path,
                           phase: str):
        try:
            pid, master_fd = pty.fork()
        except OSError as error:
            raise HarnessFailure(
                "setup",
                f"failed to allocate PTY for {phase}: {error}",
            ) from error
        if pid == 0:
            try:
                os.chdir(str(self.harness.root))
                try:
                    os.setpgrp()
                except OSError:
                    pass
                os.execvpe(argv[0], argv, self.env)
            except BaseException as error:
                message = f"exec failed for {shlex.join(argv)}: {error}\n"
                try:
                    os.write(2, message.encode("utf-8", "replace"))
                finally:
                    os._exit(127)
        return PtyProcess(pid, master_fd, output_path)

    def _replace_worker(self, index: int):
        if not isinstance(self.spec.peers, int):
            raise HarnessFailure(
                "setup",
                "replace_worker_index requires a fixed integer peer count",
            )
        if index < 0 or index >= len(self.spec.commands):
            raise HarnessFailure(
                "setup",
                f"replace_worker_index={index} outside command list",
            )
        if index >= len(self.processes):
            raise HarnessFailure(
                "setup",
                f"replace_worker_index={index} outside process list",
            )

        departed = self.processes[index]
        self._terminate_process_group(
            departed, f"replace-worker {departed.pid}")
        remaining_peers = self.spec.peers - 1
        self._wait_for_status(
            remaining_peers,
            remaining_peers > 0,
            "replace-worker-disconnect",
        )
        replacement_index = len(self.processes)
        self._launch_process(
            replacement_index,
            self.spec.commands[index],
            f"replace-worker-{replacement_index}",
        )
        self._wait_for_status(self.spec.peer_counts(), True,
                              "replace-worker")

    def _status(self) -> DmtcpStatus:
        payload = self._run_json_command("--status", "status",
                                         allow_error=False,
                                         expected_command="DMT_STATUS")
        try:
            return DmtcpStatus.from_json(payload)
        except ValueError as error:
            raise HarnessFailure("status", str(error)) from error

    def _checkpoint(self):
        self._write_checkpoint_dir_files()
        if self.spec.pre_checkpoint_delay > 0.0:
            time.sleep(self.spec.pre_checkpoint_delay)
        payload = self._run_json_command(
            self.spec.checkpoint_command,
            "checkpoint",
            allow_error=self.spec.checkpoint_kills_workers(),
            expected_command=dmtcp_command_json_command(
                self.spec.checkpoint_command,
            ),
        )
        if not DmtcpCommandJson.is_success(payload) and not (
                self.spec.checkpoint_kills_workers() and
                payload.get("command_status") == "DMT_COORD_NOT_RUNNING"):
            raise HarnessFailure("checkpoint",
                                 str(payload.get("command_status")))
        self._wait_for(lambda: bool(self._checkpoint_images()),
                       "checkpoint", "checkpoint image was not created")
        images = self._checkpoint_images()
        self._record_checkpoint_images("checkpoint", images)
        self._validate_checkpoint_images(images)
        if self.spec.checkpoint_kills_workers():
            self._wait_for_status(0, False, "checkpoint")
        else:
            self._wait_for_status(self.spec.peer_counts(), True, "checkpoint")

    def _validate_checkpoint_images(self, images: List[pathlib.Path]):
        if not self.spec.validate_checkpoint_headers and \
                self.spec.expect_checkpoint_gzip is None:
            return

        for image in images:
            with image.open("rb") as stream:
                header = stream.read(32)
            is_gzip = header.startswith(b"\x1f\x8b")
            if self.spec.expect_checkpoint_gzip is not None and \
                    is_gzip != self.spec.expect_checkpoint_gzip:
                expected = "gzip" if self.spec.expect_checkpoint_gzip \
                    else "plain"
                actual = "gzip" if is_gzip else "plain"
                raise HarnessFailure(
                    "checkpoint",
                    f"checkpoint image has {actual} format; "
                    f"expected {expected}: {image}",
                )
            if self.spec.validate_checkpoint_headers and not is_gzip and \
                    not header.startswith(b"DMTCP_CHECKPOINT_IMAGE_"):
                raise HarnessFailure(
                    "checkpoint",
                    f"checkpoint image has invalid DMTCP signature: {image}",
                )

    def _kill_workers(self, best_effort: bool = False):
        try:
            payload = self._run_json_command("--kill", "kill",
                                             allow_error=False,
                                             expected_command="DMT_KILL")
            if not DmtcpCommandJson.is_success(payload):
                raise HarnessFailure("kill",
                                     "dmtcp_command --json --kill failed")
            self._wait_for_status(0, False, "kill")
        except HarnessFailure:
            if not best_effort:
                raise

    def _quit_workers_and_coordinator(self):
        payload = self._run_json_command("--quit", "quit", allow_error=False,
                                         expected_command="DMT_QUIT")
        if not DmtcpCommandJson.is_success(payload):
            raise HarnessFailure("quit", "dmtcp_command --json --quit failed")
        if self.coordinator_proc is not None:
            try:
                self.coordinator_proc.wait(timeout=self.spec.timeout)
            except subprocess.TimeoutExpired as error:
                raise HarnessFailure(
                    "quit",
                    "coordinator did not exit after dmtcp_command --quit",
                ) from error
        self._wait_for_worker_exit("quit")

    def _kill_workers_and_wait_for_coordinator_exit(self):
        payload = self._run_json_command("--kill", "kill", allow_error=False,
                                         expected_command="DMT_KILL")
        if not DmtcpCommandJson.is_success(payload):
            raise HarnessFailure("kill",
                                 "dmtcp_command --json --kill failed")
        self._wait_for_worker_exit("kill")
        if self.coordinator_proc is not None:
            try:
                self.coordinator_proc.wait(timeout=self.spec.timeout)
            except subprocess.TimeoutExpired as error:
                raise HarnessFailure(
                    "kill",
                    "coordinator did not exit after last worker disconnected",
                ) from error

    def _complete_test(self):
        if self.spec.completion_command == "--kill":
            self._kill_workers()
        elif self.spec.completion_command == "--quit":
            self._quit_workers_and_coordinator()
        elif self.spec.completion_command == "--kill-exit-on-last":
            self._kill_workers_and_wait_for_coordinator_exit()
        else:
            raise HarnessFailure(
                "setup",
                f"unknown completion command: {self.spec.completion_command}",
            )

    def _restart(self):
        images = self._checkpoint_images()
        if not images:
            raise HarnessFailure("restart", "no checkpoint image available")
        self._record_checkpoint_images("restart", images)
        index = len(self.processes)
        restart_args = [str(self.harness.restart), "--quiet"]
        restart_args.extend(
            self._expand_arg(arg) for arg in self.spec.restart_args)
        if self.spec.restart_pause_level is not None:
            restart_args.extend([
                "--debug-restart-pause",
                str(self.spec.restart_pause_level),
            ])
        if self.spec.restart_uses_directory:
            restart_args.extend(["--restartdir", str(self.work.ckpt_dir)])
        else:
            restart_args.extend([str(path) for path in images])
        self._record_command(f"restart-worker-{index}", restart_args)
        proc = self._spawn_worker_process(
            restart_args,
            self.work.path / f"restart-{index}.out",
            "w",
            f"restart-worker-{index}",
        )
        self.processes.append(proc)
        if self.spec.restart_pause_level is not None:
            self._assert_restart_paused(proc)
            self._terminate_process_group(proc, f"restart-pause {proc.pid}")
            self._wait_for_status(0, False, "restart-pause")
            return
        self._wait_for_status(self.spec.peer_counts(), True, "restart")
        if self.spec.post_restart_validator is not None:
            self.spec.post_restart_validator(self)
        self._clear_checkpoint_dir()

    def _assert_restart_paused(self, proc: subprocess.Popen):
        start = time.time()
        deadline = time.time() + min(self.spec.timeout, 5.0)
        observed_alive = False
        while time.time() < deadline:
            if proc.poll() is not None:
                raise HarnessFailure(
                    "restart-pause",
                    "dmtcp_restart exited instead of pausing",
                )
            observed_alive = True
            status = self._status()
            if status.running:
                raise HarnessFailure(
                    "restart-pause",
                    "dmtcp_restart rejoined the coordinator instead of "
                    "pausing",
                )
            if observed_alive and time.time() - start >= 1.0:
                return
            time.sleep(0.1)
        if not observed_alive:
            raise HarnessFailure(
                "restart-pause",
                "dmtcp_restart did not stay alive long enough to verify "
                "debug pause",
            )
        raise HarnessFailure(
            "restart-pause",
            "timed out before verifying debug restart pause",
        )

    def _assert_restart_rejected_while_running(self):
        images = self._checkpoint_images()
        if not images:
            raise HarnessFailure(
                "restart-while-running",
                "no checkpoint image available for rejection check",
            )
        restart_args = [
            str(self.harness.restart),
            "--quiet",
            *[str(path) for path in images],
        ]
        self._record_command("restart-while-running", restart_args)
        result = subprocess.run(
            restart_args,
            cwd=str(self.harness.root),
            env=self.env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=self.spec.timeout,
        )
        with (self.work.path / "commands.log").open("a",
                                                    encoding="utf-8") as out:
            out.write(f"exit={result.returncode}\n")
            if result.stdout:
                out.write(result.stdout)
            if result.stderr:
                out.write("\n[stderr]\n")
                out.write(result.stderr)
            out.write("\n")
        if result.returncode == 0:
            raise HarnessFailure(
                "restart-while-running",
                "dmtcp_restart succeeded while original computation was "
                "still running",
            )
        self._assert_status(self.spec.peer_counts(), True,
                            "restart-while-running")

    def _quit_coordinator(self):
        if self.coordinator_proc is None or self.coordinator_proc.poll() is not None:
            return
        try:
            self._run_json_command("--quit", "quit", allow_error=True,
                                   expected_command="DMT_QUIT")
            self.coordinator_proc.wait(timeout=5)
        except (HarnessFailure, subprocess.TimeoutExpired):
            pass

    def _record_cleanup(self, message: str):
        with (self.work.path / "cleanup.log").open("a", encoding="utf-8") as out:
            out.write(message)
            out.write("\n")

    def _record_command(self, phase: str, argv: List[str]):
        with (self.work.path / "commands.log").open("a",
                                                    encoding="utf-8") as out:
            out.write(f"$ {shlex.join([str(arg) for arg in argv])}\n")
            out.write(f"phase={phase}\n")

    def _record_checkpoint_images(self, phase: str,
                                  images: List[pathlib.Path]):
        with (self.work.path / "checkpoint-images.log").open(
                "a", encoding="utf-8") as out:
            out.write(f"phase={phase}\n")
            for image in images:
                out.write(f"{image}\n")

    def _record_process_status(self, phase: str):
        with (self.work.path / "processes.log").open(
                "a", encoding="utf-8") as out:
            out.write(f"phase={phase}\n")
            if self.coordinator_proc is None:
                out.write("role=coordinator state=not-started\n")
            else:
                self._write_process_status(out, "coordinator",
                                           self.coordinator_proc)
            for index, proc in enumerate(self.processes):
                self._write_process_status(out, "worker", proc, index)

    def _write_process_status(self, out, role: str, proc: subprocess.Popen,
                              index: Optional[int] = None):
        returncode = proc.poll()
        state = "running" if returncode is None else "exited"
        fields = [f"role={role}"]
        if index is not None:
            fields.append(f"index={index}")
        fields.extend([
            f"pid={proc.pid}",
            f"state={state}",
            f"returncode={returncode}",
        ])
        out.write(" ".join(fields))
        out.write("\n")

    def _process_group_members(self, pgid: int) -> List[int]:
        members = []
        proc_root = pathlib.Path("/proc")
        if not proc_root.exists():
            return members
        for entry in proc_root.iterdir():
            if not entry.name.isdigit():
                continue
            try:
                stat = (entry / "stat").read_text(encoding="utf-8")
                close_paren = stat.rfind(")")
                if close_paren == -1:
                    continue
                fields = stat[close_paren + 2:].split()
                if len(fields) >= 3 and int(fields[2]) == pgid:
                    members.append(int(entry.name))
            except (OSError, ValueError):
                continue
        return members

    def _terminate_process_group(self, proc: subprocess.Popen, label: str):
        pgid = proc.pid
        if proc.poll() is None:
            self._record_cleanup(f"{label}: SIGTERM process group {pgid}")
            try:
                os.killpg(pgid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self._record_cleanup(f"{label}: SIGKILL process group {pgid}")
                try:
                    os.killpg(pgid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                proc.wait(timeout=5)

        leftovers = self._process_group_members(pgid)
        if leftovers:
            self._record_cleanup(
                f"{label}: leftover process group {pgid}: {leftovers}")
            try:
                os.killpg(pgid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            time.sleep(0.1)
            leftovers = self._process_group_members(pgid)
            if leftovers:
                raise HarnessFailure(
                    "cleanup",
                    f"leftover processes in group {pgid}: {leftovers}",
                )
        else:
            self._record_cleanup(f"{label}: process group {pgid} clean")

    def _run_json_command(self, command: str, phase: str,
                          allow_error: bool,
                          expected_command: Optional[str] = None
                          ) -> Dict[str, object]:
        transcript = self.work.path / "commands.log"
        try:
            result = subprocess.run(
                [str(self.harness.command), "--json", command],
                cwd=str(self.harness.root),
                env=self.env,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
                timeout=self.spec.timeout,
            )
        except subprocess.TimeoutExpired as error:
            with transcript.open("a", encoding="utf-8") as out:
                out.write(f"$ dmtcp_command --json {command}\n")
                out.write(f"timeout={self.spec.timeout}\n")
                if error.stdout:
                    out.write(str(error.stdout))
                if error.stderr:
                    out.write("\n[stderr]\n")
                    out.write(str(error.stderr))
                out.write("\n")
            raise HarnessFailure(
                phase,
                self._with_diagnostics(
                    f"dmtcp_command --json {command} timed out after "
                    f"{self.spec.timeout} seconds"
                ),
            )
        with transcript.open("a", encoding="utf-8") as out:
            out.write(f"$ dmtcp_command --json {command}\n")
            out.write(f"exit={result.returncode}\n")
            out.write(result.stdout)
            if result.stderr:
                out.write("\n[stderr]\n")
                out.write(result.stderr)
            out.write("\n")
        try:
            payload = DmtcpCommandJson.parse(result.stdout)
        except ValueError as error:
            raise HarnessFailure(phase, str(error))
        try:
            expected = expected_command or dmtcp_command_json_command(command)
            DmtcpCommandJson.validate_result(payload, expected)
        except ValueError as error:
            raise HarnessFailure(phase, str(error))
        success = DmtcpCommandJson.is_success(payload)
        if result.returncode != 0 and success and not allow_error:
            raise HarnessFailure(
                phase,
                self._with_diagnostics(
                    f"dmtcp_command --json {command} exited with "
                    f"exit code {result.returncode} despite "
                    "DMT_COORD_SUCCESS"
                ),
            )
        if (result.returncode != 0 or not success) and not allow_error:
            message = payload.get("command_status", result.stderr)
            raise HarnessFailure(phase, self._with_diagnostics(str(message)))
        return payload

    def _assert_status(self, peers: Union[int, List[int]], running: bool,
                       phase: str):
        status = self._status()
        peer_counts = [peers] if isinstance(peers, int) else peers
        if status.num_peers not in peer_counts or status.running != running:
            raise HarnessFailure(
                phase,
                self._with_diagnostics(
                    f"expected peers={peers} running={running}; "
                    f"got peers={status.num_peers} running={status.running}",
                    last_status=status,
                ),
            )

    def _wait_for_status(self, peers: Union[int, List[int]], running: bool,
                         phase: str):
        peer_counts = [peers] if isinstance(peers, int) else peers
        last_status = None
        deadline = time.time() + self.spec.timeout
        while time.time() < deadline:
            try:
                status = self._status()
            except HarnessFailure as error:
                raise HarnessFailure(
                    phase,
                    self._with_diagnostics(
                        f"status command failed: {error.message}"
                    ),
                ) from error
            last_status = status
            if status.num_peers in peer_counts and status.running == running:
                return
            time.sleep(0.1)

        raise HarnessFailure(
            phase,
            self._with_diagnostics(
                f"timed out waiting for peers={peers} running={running}",
                last_status=last_status,
            ),
        )

    def _wait_for_worker_exit(self, phase: str):
        def workers_stopped() -> bool:
            return all(proc.poll() is not None for proc in self.processes)

        self._wait_for(workers_stopped, phase,
                       "timed out waiting for workers to exit")
        for proc in self.processes:
            leftovers = self._process_group_members(proc.pid)
            if leftovers:
                raise HarnessFailure(
                    phase,
                    f"leftover worker process group {proc.pid}: {leftovers}",
                )

    def _wait_for(self, predicate: Callable[[], bool], phase: str,
                  message: str):
        deadline = time.time() + self.spec.timeout
        while time.time() < deadline:
            if predicate():
                return
            time.sleep(0.1)
        raise HarnessFailure(phase, self._with_diagnostics(message))

    def _with_diagnostics(
            self, message: str,
            last_status: Optional[DmtcpStatus] = None) -> str:
        diagnostics = self._failure_diagnostics(last_status)
        if not diagnostics:
            return message
        return f"{message}; {diagnostics}"

    def _failure_diagnostics(
            self,
            last_status: Optional[DmtcpStatus] = None) -> str:
        fields = []
        if last_status is not None:
            fields.append(
                f"last_status=peers:{last_status.num_peers},"
                f"running:{last_status.running},"
                f"interval:{last_status.checkpoint_interval}"
            )
        if self.port is not None:
            fields.append(f"port={self.port}")
        fields.append(self._process_summary("coordinator",
                                            self.coordinator_proc))
        if self.processes:
            workers = [
                self._process_summary("worker", proc, index)
                for index, proc in enumerate(self.processes)
            ]
            fields.append(f"workers=[{', '.join(workers)}]")
        else:
            fields.append("workers=[]")
        return "; ".join(fields)

    @staticmethod
    def _process_summary(role: str, proc, index: Optional[int] = None) -> str:
        if proc is None:
            return f"{role}=not-started"
        returncode = proc.poll()
        state = "running" if returncode is None else f"exited:{returncode}"
        label = role if index is None else f"{role}{index}"
        return f"{label}=pid:{proc.pid},state:{state}"

    def _checkpoint_images(self) -> List[pathlib.Path]:
        return sorted(self.work.path.glob("**/ckpt_*.dmtcp"))

    def _write_checkpoint_dir_files(self):
        for relative_path, contents in self.spec.checkpoint_dir_files.items():
            path = pathlib.Path(relative_path)
            if path.is_absolute() or ".." in path.parts:
                raise HarnessFailure(
                    "setup",
                    f"invalid checkpoint file path: {relative_path}",
                )
            output_path = self.work.ckpt_dir / path
            output_path.parent.mkdir(parents=True, exist_ok=True)
            output_path.write_text(
                contents.replace("{workdir}", str(self.work.path)),
                encoding="utf-8",
            )

    def _clear_checkpoint_dir(self):
        for path in self.work.ckpt_dir.iterdir():
            if path.is_dir():
                shutil.rmtree(path)
            else:
                path.unlink()


def read_required_artifact(context: TestContext, name: str) -> str:
    path = context.work.path / name
    if not path.exists():
        raise HarnessFailure("validate", f"missing artifact: {path}")
    return path.read_text(encoding="utf-8", errors="replace")


def require_text(text: str, needle: str, label: str):
    if needle not in text:
        raise HarnessFailure("validate",
                             f"missing {needle!r} in {label}")


def reject_text(text: str, needle: str, label: str):
    if needle in text:
        raise HarnessFailure("validate",
                             f"unexpected {needle!r} in {label}")


def require_text_count(text: str, needle: str, minimum: int, label: str):
    count = text.count(needle)
    if count < minimum:
        raise HarnessFailure(
            "validate",
            f"expected at least {minimum} copies of {needle!r} in {label}; "
            f"found {count}",
        )


def validate_trace_logging_restart(context: TestContext):
    log = read_required_artifact(context, "runtime.log")
    require_text(log, "TRACE", "runtime.log")
    require_text(log, "starting checkpoint; incrementing generation",
                 "runtime.log")
    require_text(log, "Checkpoint complete; all workers running",
                 "runtime.log")
    require_text(log, "New dmtcp_restart process", "runtime.log")
    require_text(log, "FIRST restart connection", "runtime.log")
    require_text_count(log, "Worker resumed execution", 2, "runtime.log")

    console = (
        read_required_artifact(context, "worker-0.out") +
        read_required_artifact(context, "restart-1.out")
    )
    require_text(console, "TRACE", "worker/restart console output")


def validate_quiet_logging(context: TestContext):
    log = read_required_artifact(context, "quiet.log")
    for severity in ("TRACE", "NOTE", "WARNING"):
        reject_text(log, severity, "quiet.log")


def validate_log_overrides(context: TestContext):
    log = read_required_artifact(context, "override.log")
    require_text(log, "TRACE", "override.log")
    require_text(log, "libdmtcp.so: Running program", "override.log")


def checkpoint_image_paths(context: TestContext) -> List[pathlib.Path]:
    log = read_required_artifact(context, "checkpoint-images.log")
    paths = []
    for line in log.splitlines():
        if line.startswith("phase=") or not line:
            continue
        paths.append(pathlib.Path(line))
    return paths


def validate_unique_checkpoint_subdir(context: TestContext):
    image_paths = checkpoint_image_paths(context)
    if not image_paths:
        raise HarnessFailure("validate", "no checkpoint images recorded")

    for image in image_paths:
        if image.parent == context.work.ckpt_dir:
            raise HarnessFailure(
                "validate",
                f"checkpoint image was not placed in a unique directory: "
                f"{image}",
            )
        if image.parent.parent != context.work.ckpt_dir:
            raise HarnessFailure(
                "validate",
                f"unique checkpoint directory is outside checkpoint root: "
                f"{image.parent}",
            )
        if not image.parent.name.startswith("ckpt_"):
            raise HarnessFailure(
                "validate",
                f"unique checkpoint directory has unexpected name: "
                f"{image.parent}",
            )


def validate_tmpdir_is_private(context: TestContext):
    tmpdir = pathlib.Path(context.env["DMTCP_TMPDIR"])
    if not tmpdir.exists():
        raise HarnessFailure("validate", f"missing tmpdir: {tmpdir}")
    if not tmpdir.is_relative_to(context.work.path):
        raise HarnessFailure("validate", f"tmpdir escaped workdir: {tmpdir}")


def validate_restart_tmpdir(context: TestContext):
    wait_for_success_artifact(context, "DMTCP_RESTART_TMPDIR_SUCCESS_FILE",
                              "restart-tmpdir")


def wait_for_success_artifact(context: TestContext, env_name: str,
                              phase: str):
    path = pathlib.Path(context.env[env_name])

    def success_marker_ready() -> bool:
        try:
            contents = path.read_text(encoding="utf-8")
        except OSError:
            return False
        return "ok" in contents

    context._wait_for(
        success_marker_ready,
        phase,
        f"missing success artifact with ok marker: {path}",
    )


def validate_modify_env_restart(context: TestContext):
    wait_for_success_artifact(context, "DMTCP_MODIFY_ENV_SUCCESS_FILE",
                              "modify-env")


def validate_pathvirt_restart(context: TestContext):
    wait_for_success_artifact(context, "DMTCP_PATHVIRT_SUCCESS_FILE",
                              "pathvirt")


class TestRegistry:
    DISABLED_CATEGORY = "Disabled tests"

    M32_DISABLED_TESTS = frozenset({
        "dlopen2",
        "ssh1",
        "waitpid",
        "waitid-syscall",
        "gzip",
        "readline",
        "perl",
        "python",
        "bash",
        "dash",
        "zsh",
        "tcsh",
        "script",
        "vim",
        "emacs",
        "screen",
        "cilk1",
        "matlab-nodisplay",
        "hellompich-n1",
        "hellompich-n2",
        "openmpi",
    })

    CATEGORY_BY_TEST = {
        "command-json-kill": COORDINATOR_PROTOCOL_CATEGORY,
        "command-json-quit": COORDINATOR_PROTOCOL_CATEGORY,
        "command-json-bcheckpoint": COORDINATOR_PROTOCOL_CATEGORY,
        "coordinator-exit-on-last": COORDINATOR_PROTOCOL_CATEGORY,
        "coordinator-replacement-worker": COORDINATOR_PROTOCOL_CATEGORY,
        "coordinator-reject-restart-while-running":
            COORDINATOR_PROTOCOL_CATEGORY,
        "coordinator-barrier": COORDINATOR_PROTOCOL_CATEGORY,
        "logging-runtime": "Logging",
        "logging-quiet": "Logging",
        "logging-overrides": "Logging",
        "sched_test": "Multi-process coordination",
        "dmtcp5": "Multi-process coordination",
        "shared-fd1": "Multi-process coordination",
        "shared-fd2": "Multi-process coordination",
        "stale-fd": "Multi-process coordination",
        "rlimit-nofile": "Multi-process coordination",
        "procfd1": "Multi-process coordination",
        "forkexec": "Multi-process coordination",
        "client-server": "Multi-process coordination",
        "seqpacket": "Multi-process coordination",
        "presuspend": "Multi-process coordination",
        "popen1": "Multi-process coordination",
        "restartdir": "Multi-process coordination",
        "vfork1": "Multi-process coordination",
        "vfork2": "Multi-process coordination",
        "frisbee": "Multi-process coordination",
        "epoll1": "IPC and shared resources",
        "epoll2": "IPC and shared resources",
        "shared-memory1": "IPC and shared resources",
        "shared-memory2": "IPC and shared resources",
        "shared-memory3": "IPC and shared resources",
        "sysv-shm1": "IPC and shared resources",
        "sysv-shm2": "IPC and shared resources",
        "sysv-sem": "IPC and shared resources",
        "sysv-msg": "IPC and shared resources",
        "posix-mq1": "IPC and shared resources",
        "posix-mq-close-untracked": "IPC and shared resources",
        "cma": "IPC and shared resources",
        "pty1": "PTY handling",
        "pty2": "PTY handling",
        "pthread1": "Thread and mutex tests",
        "pthread2": "Thread and mutex tests",
        "pthread3": "Thread and mutex tests",
        "pthread4": "Thread and mutex tests",
        "pthread5": "Thread and mutex tests",
        "pthread6": "Thread and mutex tests",
        "mutex1": "Thread and mutex tests",
        "mutex2": "Thread and mutex tests",
        "mutex3": "Thread and mutex tests",
        "mutex4": "Thread and mutex tests",
        "pthread_atfork1": "Thread and mutex tests",
        "pthread_atfork2": "Thread and mutex tests",
        "plugin-sleep2": "Plugin behavior",
        "plugin-example-db": "Plugin behavior",
        "plugin-init": "Plugin behavior",
        "modify-env": "Plugin behavior",
        "pathvirt": "Plugin behavior",
        "poll-disable-event-plugin": "Plugin behavior",
        "syscall-tester": "Checkpoint mechanics",
        "nocheckpoint": "Checkpoint mechanics",
        "checkpoint-header": "Checkpoint mechanics",
        "restart-debug-pause": "Checkpoint mechanics",
        "restart-no-strict-checking": "Checkpoint mechanics",
        "restart-tmpdir-flag": "Checkpoint mechanics",
        "ckptdir-flag": "Checkpoint mechanics",
        "ckpt-signal-flag": "Checkpoint mechanics",
        "gzip-flag": "Checkpoint mechanics",
        "no-gzip-flag": "Checkpoint mechanics",
        "tmpdir-env": "Checkpoint mechanics",
        "unique-ckpt-env": "Checkpoint mechanics",
        "unique-ckpt-flag": "Checkpoint mechanics",
        "dmtcp1-m32": "Build variants",
        "gzip": "Checkpoint mechanics",
        "waitpid": "Process control and signals",
        "waitid-syscall": "Process control and signals",
        "ssh1": "Process control and signals",
        "readline": "Terminal-oriented programs",
        "bash": "Interactive shells",
        "dash": "Interactive shells",
        "zsh": "Interactive shells",
        "tcsh": "Interactive shells",
        "script": "Interactive shells",
        "vim": "Terminal-oriented programs",
        "emacs": "Terminal-oriented programs",
        "screen": "Terminal-oriented programs",
        "perl": "Language runtimes",
        "python": "Language runtimes",
        "java1": "Language runtimes",
        "matlab-nodisplay": "Language runtimes",
        "cilk1": "Parallel runtimes",
        "hellompich-n1": "Parallel runtimes",
        "hellompich-n2": "Parallel runtimes",
        "openmpi": "Parallel runtimes",
        "openmp-1": "Parallel runtimes",
        "openmp-2": "Parallel runtimes",
    }

    def __init__(self, tests: Optional[Iterable[TestSpec]] = None):
        if tests is None:
            tests = self._build_tests()
        tests = self._categorize_tests(tests)
        self._tests = []
        self._disabled_tests = []
        for test in tests:
            reasons = self._disabled_reasons(test)
            if reasons:
                self._disabled_tests.append(
                    replace(
                        test,
                        category=self.DISABLED_CATEGORY,
                        list_notes=reasons,
                    )
                )
            else:
                self._tests.append(test)

    @staticmethod
    def _config_yes(name: str) -> bool:
        return getattr(autotest_config, name, "no") == "yes"

    @staticmethod
    def _use_m32() -> bool:
        return bool(getattr(autotest_config, "USE_M32", 0))

    @classmethod
    def _filter_m32_disabled_tests(
            cls, tests: Iterable[TestSpec]) -> List[TestSpec]:
        if not cls._use_m32():
            return list(tests)
        return [
            test for test in tests
            if test.name not in cls.M32_DISABLED_TESTS
        ]

    @staticmethod
    def _command_executables_available(command: str) -> bool:
        return not TestRegistry._command_executable_reasons(command)

    @staticmethod
    def _display_required_path(path: pathlib.Path) -> str:
        if path.is_absolute():
            return str(path)
        display_path = str(path)
        if display_path.startswith("./"):
            return display_path[2:]
        return display_path

    @staticmethod
    def _command_executable_reasons(command: str) -> List[str]:
        argv = shlex.split(command)
        if not argv:
            return []
        reasons = []
        executable = pathlib.Path(argv[0])
        if executable.is_absolute() and not executable.exists():
            reasons.append(f"missing {executable}")
        for token in argv:
            if not (token.startswith("./test/") or
                    token.startswith("test/")):
                continue
            test_binary = pathlib.Path(token)
            display_path = TestRegistry._display_required_path(test_binary)
            if not test_binary.is_absolute():
                test_binary = ROOT / test_binary
            if not test_binary.exists():
                reasons.append(f"missing {display_path}")
        return list(dict.fromkeys(reasons))

    @classmethod
    def _filter_unavailable_command_executables(
            cls, tests: Iterable[TestSpec]) -> List[TestSpec]:
        return [
            test for test in tests
            if all(
                cls._command_executables_available(command)
                for command in test.commands
            )
        ]

    @staticmethod
    def _required_file_available(path: str) -> bool:
        required_path = pathlib.Path(path)
        if not required_path.is_absolute():
            required_path = ROOT / required_path
        return required_path.exists()

    @staticmethod
    def _required_file_reason(path: str) -> Optional[str]:
        if TestRegistry._required_file_available(path):
            return None
        return f"missing {TestRegistry._display_required_path(pathlib.Path(path))}"

    @classmethod
    def _filter_unavailable_required_files(
            cls, tests: Iterable[TestSpec]) -> List[TestSpec]:
        return [
            test for test in tests
            if all(
                cls._required_file_available(path)
                for path in test.required_files
            )
        ]

    @classmethod
    def _filter_unavailable_configure_flags(
            cls, tests: Iterable[TestSpec]) -> List[TestSpec]:
        return [
            test for test in tests
            if all(cls._config_yes(flag) for flag in test.configure_flags) and
            not any(
                cls._config_yes(flag)
                for flag in test.blocked_configure_flags
            )
        ]

    @classmethod
    def _filter_tests(cls, tests: Iterable[TestSpec]) -> List[TestSpec]:
        return cls._filter_m32_disabled_tests(
            cls._filter_unavailable_command_executables(
                cls._filter_unavailable_required_files(
                    cls._filter_unavailable_configure_flags(tests))))

    @classmethod
    def _disabled_reasons(cls, test: TestSpec) -> List[str]:
        reasons = []
        if cls._use_m32() and test.name in cls.M32_DISABLED_TESTS:
            reasons.append("disabled for m32")
            return reasons
        reasons.extend(
            f"requires {flag}"
            for flag in test.configure_flags
            if not cls._config_yes(flag)
        )
        reasons.extend(
            f"blocked by {flag}"
            for flag in test.blocked_configure_flags
            if cls._config_yes(flag)
        )
        if reasons:
            return list(dict.fromkeys(reasons))
        for command in test.commands:
            reasons.extend(cls._command_executable_reasons(command))
        for path in test.required_files:
            reason = cls._required_file_reason(path)
            if reason:
                reasons.append(reason)
        return list(dict.fromkeys(reasons))

    @classmethod
    def _categorize_tests(cls, tests: Iterable[TestSpec]) -> List[TestSpec]:
        return [
            replace(
                test,
                category=cls.CATEGORY_BY_TEST.get(test.name, test.category),
            )
            for test in tests
        ]

    def _build_tests(self) -> List[TestSpec]:
        frisbee_p1, frisbee_p2, frisbee_p3 = [
            str(port) for port in sample(range(2000, 10000), 3)
        ]

        tests = [
            TestSpec("dmtcp1", 1, ["./test/dmtcp1"]),
            TestSpec("dmtcp1-trace", 1, ["./test/dmtcp1"],
                     env={"DMTCP_LOG_LEVEL": "trace"},
                     list_notes=["runtime trace logging"]),
            TestSpec("dmtcp1-quiet", 1, ["./test/dmtcp1"],
                     env={"DMTCP_QUIET": "2"},
                     list_notes=["quiet logging"]),
            TestSpec("logging-runtime", 1, ["./test/pthread2 8"],
                     cycles=1,
                     pre_checkpoint_delay=0.3,
                     env={
                         "DMTCP_LOG_FILE": "{workdir}/runtime.log",
                         "DMTCP_LOG_LEVEL": "trace",
                         "JALIB_STDERR_PATH": None,
                     },
                     post_run_validator=validate_trace_logging_restart,
                     tags=["logging", "pthread"],
                     requirements=["real-worker"],
                     limits=["cycles=1"],
                     list_notes=["trace log validation"]),
            TestSpec("logging-quiet", 1, ["./test/dmtcp1"],
                     cycles=0,
                     env={
                         "DMTCP_LOG_FILE": "{workdir}/quiet.log",
                         "DMTCP_QUIET": "2",
                     },
                     post_run_validator=validate_quiet_logging,
                     tags=["logging"],
                     requirements=["real-worker"],
                     limits=["cycles=0"],
                     list_notes=["quiet log validation"]),
            TestSpec("logging-overrides", 1, ["./test/dmtcp1"],
                     cycles=0,
                     env={
                         "DMTCP_LOG_FILE": "{workdir}/override.log",
                         "DMTCP_LOG_LEVEL": "error",
                         "DMTCP_LOG_OVERRIDES": "core=trace",
                     },
                     post_run_validator=validate_log_overrides,
                     tags=["logging"],
                     requirements=["real-worker"],
                     limits=["cycles=0"],
                     list_notes=["component log override"]),
            TestSpec("command-json-kill", 1, ["./test/dmtcp1"], cycles=0,
                     limits=["cycles=0"]),
            TestSpec("command-json-quit", 1, ["./test/dmtcp1"], cycles=0,
                     completion_command="--quit",
                     limits=["cycles=0"]),
            TestSpec("command-json-bcheckpoint", 1, ["./test/dmtcp1"],
                     cycles=1,
                     checkpoint_command="--bcheckpoint",
                     tags=["command-json", "checkpoint"],
                     requirements=["real-worker"],
                     limits=["cycles=1"]),
            TestSpec("coordinator-exit-on-last", 1, ["./test/dmtcp1"],
                     cycles=0,
                     completion_command="--kill-exit-on-last",
                     coordinator_args=["--exit-on-last"],
                     tags=["coordinator", "exit-on-last"],
                     requirements=["real-worker"],
                     limits=["cycles=0"]),
            TestSpec("coordinator-replacement-worker", 2,
                     ["./test/dmtcp1", "./test/dmtcp1"],
                     cycles=0,
                     replace_worker_index=0,
                     tags=["coordinator", "replacement-worker"],
                     requirements=["real-worker"],
                     limits=["cycles=0"]),
            TestSpec("coordinator-reject-restart-while-running", 1,
                     ["./test/dmtcp1"],
                     cycles=0,
                     reject_restart_while_running=True,
                     tags=["coordinator", "restart-rejection"],
                     requirements=["real-worker"],
                     limits=["cycles=0"]),
            TestSpec("dmtcp2", 1, ["./test/dmtcp2"]),
            TestSpec("dmtcp3", 1, ["./test/dmtcp3"]),
            TestSpec("dmtcp4", 1, ["./test/dmtcp4"]),
            TestSpec("alarm", 1, ["./test/alarm"]),
            TestSpec("sched_test", 2, ["./test/sched_test"]),
            TestSpec("coordinator-barrier", 2, ["./test/sched_test"],
                     cycles=1,
                     tags=["coordinator", "barrier"],
                     requirements=["real-worker"],
                     limits=["cycles=1"]),
            TestSpec("gettid", 1, ["./test/gettid"]),
            TestSpec("file1", 1, ["./test/file1"]),
            TestSpec("file3", 1, ["./test/file3"]),
            TestSpec("stat", 1, ["./test/stat"]),
            TestSpec("mmap1", 1, ["./test/mmap1"]),
            TestSpec("mremap", 1, ["./test/mremap"]),
            TestSpec("gettimeofday", 1, ["./test/gettimeofday"]),
            TestSpec("sigchild", 1, ["./test/sigchild"]),
            TestSpec("rlimit-restore", 1, ["./test/rlimit-restore"]),
            TestSpec("poll", 1, ["./test/poll"]),
            TestSpec("environ", 1, ["./test/environ"]),
            TestSpec("realpath", 1, ["./test/realpath"]),
            TestSpec("pthread1", 1, ["./test/pthread1"]),
            TestSpec("pthread2", 1, ["./test/pthread2"]),
            TestSpec("pthread4", 1, ["./test/pthread4"]),
            TestSpec("pthread5", 1, ["./test/pthread5"]),
            TestSpec("pthread6", 1, ["./test/pthread6"]),
            TestSpec("mutex1", 1, ["./test/mutex1"]),
            TestSpec("mutex2", 1, ["./test/mutex2"]),
            TestSpec("mutex3", 1, ["./test/mutex3"]),
            TestSpec("mutex4", 1, ["./test/mutex4"]),
            TestSpec("timer1", 1, ["./test/timer1"]),
            TestSpec("clock", 1, ["./test/clock"]),
            TestSpec("dlopen1", 1, ["./test/dlopen1"]),
            TestSpec("dmtcp5", 2, ["./test/dmtcp5"]),
            TestSpec("syscall-tester", 1,
                     ["--checkpoint-open-files ./test/syscall-tester"],
                     checkpoint_command="--kcheckpoint"),
            TestSpec("shared-fd1", 2, ["./test/shared-fd1"]),
            TestSpec("shared-fd2", 2, ["./test/shared-fd2"]),
            TestSpec("stale-fd", 2, ["./test/stale-fd"]),
            TestSpec("rlimit-nofile", 2, ["./test/rlimit-nofile"]),
            TestSpec("procfd1", 2, ["./test/procfd1"]),
            TestSpec("epoll1", 2, ["./test/epoll1"]),
            TestSpec("forkexec", 2, ["./test/forkexec"]),
            TestSpec("client-server", 2, ["./test/client-server"]),
            TestSpec("seqpacket", 2, ["./test/seqpacket"]),
            TestSpec("shared-memory1", 2, ["./test/shared-memory1"],
                     tags=["slow"]),
            TestSpec("shared-memory2", 2, ["./test/shared-memory2"],
                     tags=["slow"]),
            TestSpec("shared-memory3", 2, ["./test/shared-memory3"],
                     pre_checkpoint_delay=3.0,
                     tags=["slow"]),
            TestSpec("sysv-shm1", 2, ["./test/sysv-shm1"]),
            TestSpec("sysv-shm2", 2, ["./test/sysv-shm2"]),
            TestSpec("sysv-sem", 2, ["./test/sysv-sem"]),
            TestSpec("sysv-msg", 2, ["./test/sysv-msg"]),
            TestSpec("file2", 1, ["./test/file2"],
                     pre_checkpoint_delay=3.0,
                     tags=["slow"]),
            TestSpec("presuspend", [1, 2], ["./test/presuspend"]),
            TestSpec("plugin-sleep2", 1,
                     [
                         "--with-plugin "
                         f"{ROOT}/test/plugin/sleep1/libdmtcp_sleep1.so:"
                         f"{ROOT}/test/plugin/sleep2/libdmtcp_sleep2.so "
                         "./test/dmtcp1"
                     ]),
            TestSpec("plugin-example-db", 2,
                     [
                         "--with-plugin "
                         f"{ROOT}/test/plugin/example-db/"
                         "libdmtcp_example-db.so "
                         "env EXAMPLE_DB_KEY=1 EXAMPLE_DB_KEY_OTHER=2 "
                         "./test/dmtcp1",
                         "--with-plugin "
                         f"{ROOT}/test/plugin/example-db/"
                         "libdmtcp_example-db.so "
                         "env EXAMPLE_DB_KEY=2 EXAMPLE_DB_KEY_OTHER=1 "
                         "./test/dmtcp1",
                     ]),
            TestSpec("plugin-init", 1,
                     [
                         f"--with-plugin {ROOT}/test/"
                         "libdmtcp_plugin-init.so ./test/dmtcp1"
                     ]),
            TestSpec("modify-env", 1, ["--modify-env ./test/modify-env1"],
                     cycles=1,
                     env={
                         "DMTCP_MODIFY_ENV_TARGET": "before-restart",
                         "DMTCP_MODIFY_ENV_REMOVE": "remove-me",
                         "DMTCP_MODIFY_ENV_SUCCESS_FILE":
                         "{workdir}/modify-env.success",
                     },
                     checkpoint_dir_files={
                         "dmtcp_env.txt":
                         "DMTCP_MODIFY_ENV_TARGET=after-restart\n"
                         "DMTCP_MODIFY_ENV_REMOVE\n",
                     },
                     post_restart_validator=validate_modify_env_restart,
                     tags=["plugin"],
                     requirements=["real-worker"],
                     limits=["cycles=1"],
                     list_notes=["modify-env plugin"]),
            TestSpec("pathvirt", 1, ["--pathvirt ./test/pathvirt1"],
                     cycles=1,
                     env={
                         "DMTCP_PATH_MAPPING":
                         "{workdir}/virtual:{workdir}/physical",
                         "DMTCP_PATHVIRT_REAL_DIR":
                         "{workdir}/physical",
                         "DMTCP_PATHVIRT_REAL_FILE":
                         "{workdir}/physical/pathvirt-data.txt",
                         "DMTCP_PATHVIRT_VIRTUAL_FILE":
                         "{workdir}/virtual/pathvirt-data.txt",
                         "DMTCP_PATHVIRT_SUCCESS_FILE":
                         "{workdir}/pathvirt.success",
                     },
                     post_restart_validator=validate_pathvirt_restart,
                     tags=["plugin"],
                     requirements=["real-worker"],
                     limits=["cycles=1"],
                     list_notes=["pathvirt plugin"]),
            TestSpec("popen1", [1, 2], ["./test/popen1"]),
            TestSpec("poll-disable-event-plugin", 1,
                     ["--disable-event-plugin ./test/poll"]),
            TestSpec("pthread3", 1, ["./test/pthread2 80"]),
            TestSpec("restartdir", 1, ["./test/dmtcp1"],
                     restart_uses_directory=True),
            TestSpec("restart-no-strict-checking", 1, ["./test/dmtcp1"],
                     cycles=1,
                     restart_args=["--no-strict-checking"],
                     tags=["restart-options"],
                     requirements=["real-worker"],
                     limits=["cycles=1"],
                     list_notes=["restart --no-strict-checking"]),
            TestSpec("restart-tmpdir-flag", 1,
                     ["./test/restart-tmpdir"],
                     cycles=1,
                     restart_args=["--tmpdir", "{workdir}/restart-tmp"],
                     env={
                         "DMTCP_RESTART_TMPDIR_ROOT":
                         "{workdir}/restart-tmp",
                         "DMTCP_RESTART_TMPDIR_SUCCESS_FILE":
                         "{workdir}/restart-tmpdir.success",
                     },
                     post_restart_validator=validate_restart_tmpdir,
                     tags=["restart-options"],
                     requirements=["real-worker"],
                     limits=["cycles=1"],
                     list_notes=["restart --tmpdir"]),
            TestSpec("pty1", 2, ["./test/pty1"]),
            TestSpec("pty2", 2, ["./test/pty2"]),
            TestSpec("vfork1", [1, 2, 3, 4], ["./test/vfork1 'ls | wc'"]),
            TestSpec("vfork2", [2, 3, 4],
                     ["./test/vfork1 "
                      "'while true; do date; sleep 1; done'"]),
            TestSpec("frisbee", 3,
                     [
                         f"./test/frisbee {frisbee_p1} localhost {frisbee_p2}",
                         f"./test/frisbee {frisbee_p2} localhost {frisbee_p3}",
                         f"./test/frisbee {frisbee_p3} localhost {frisbee_p1} "
                         "starter",
                     ],
                     env={"DMTCP_GZIP": "1"}, post_launch_delay=2.0),
            TestSpec("nocheckpoint", [1, 2], ["./test/nocheckpoint"],
                     cycles=1,
                     limits=["cycles=1"]),
            TestSpec("checkpoint-header", 1, ["./test/dmtcp1"], cycles=1,
                     env={"DMTCP_GZIP": "0"},
                     validate_checkpoint_headers=True,
                     expect_checkpoint_gzip=False,
                     tags=["checkpoint-header"],
                     requirements=["plain-checkpoint-image"],
                     limits=["cycles=1"]),
            TestSpec("restart-debug-pause", 1, ["./test/dmtcp1"],
                     cycles=1,
                     restart_pause_level=1,
                     tags=["restart-debug"],
                     requirements=["real-worker"],
                     limits=["cycles=1"],
                     list_notes=["debug restart pause"]),
            TestSpec("ckptdir-flag", 1,
                     ["--ckptdir {workdir}/launch-ckpt ./test/dmtcp1"],
                     cycles=1,
                     env={"DMTCP_GZIP": "0"},
                     expect_checkpoint_gzip=False,
                     tags=["launcher-options"],
                     requirements=["real-worker"],
                     limits=["cycles=1"],
                     list_notes=["launcher --ckptdir"]),
            TestSpec("ckpt-signal-flag", 1,
                     ["--ckpt-signal 10 ./test/dmtcp1"],
                     cycles=1,
                     tags=["launcher-options"],
                     requirements=["real-worker"],
                     limits=["cycles=1"],
                     list_notes=["launcher --ckpt-signal"]),
            TestSpec("gzip-flag", 1, ["--gzip ./test/dmtcp1"],
                     cycles=1,
                     expect_checkpoint_gzip=True,
                     tags=["launcher-options"],
                     requirements=["real-worker"],
                     blocked_configure_flags=["AARCH64_HOST"],
                     limits=["cycles=1"],
                     list_notes=["launcher --gzip"]),
            TestSpec("no-gzip-flag", 1, ["--no-gzip ./test/dmtcp1"],
                     cycles=1,
                     expect_checkpoint_gzip=False,
                     tags=["launcher-options"],
                     requirements=["real-worker"],
                     limits=["cycles=1"],
                     list_notes=["launcher --no-gzip"]),
            TestSpec("tmpdir-env", 1, ["./test/dmtcp1"],
                     cycles=1,
                     env={"DMTCP_TMPDIR": "{workdir}/tmp"},
                     post_run_validator=validate_tmpdir_is_private,
                     tags=["runtime-env"],
                     requirements=["real-worker"],
                     limits=["cycles=1"],
                     list_notes=["DMTCP_TMPDIR"]),
            TestSpec("unique-ckpt-env", 1, ["./test/dmtcp1"],
                     cycles=1,
                     env={"DMTCP_UNIQUE_CKPT_PLUGIN": "1"},
                     post_run_validator=validate_unique_checkpoint_subdir,
                     tags=["runtime-env"],
                     requirements=["real-worker"],
                     limits=["cycles=1"],
                     list_notes=["DMTCP_UNIQUE_CKPT_PLUGIN"]),
            TestSpec("unique-ckpt-flag", 1,
                     ["--enable-unique-checkpoint-filenames ./test/dmtcp1"],
                     cycles=1,
                     post_run_validator=validate_unique_checkpoint_subdir,
                     tags=["launcher-options"],
                     requirements=["real-worker"],
                     limits=["cycles=1"],
                     list_notes=["unique checkpoint flag"]),
            TestSpec("dmtcp1-m32", 1, ["./test/dmtcp1-m32"]),
            TestSpec("epoll2", 2, ["./test/epoll1 --use-epoll-create1"],
                     configure_flags=["HAS_EPOLL_CREATE1"]),
            TestSpec("dlopen2", 1, ["./test/dlopen2"]),
            TestSpec("selinux1", 1, ["./test/selinux1"]),
            TestSpec("cma", 2, ["./test/cma"]),
            TestSpec("posix-mq1", 2, ["./test/posix-mq1"],
                     configure_flags=["TEST_POSIX_MQ"],
                     blocked_configure_flags=["ARM_HOST"]),
            TestSpec("posix-mq-close-untracked", 1,
                     ["./test/posix-mq-close-untracked"],
                     configure_flags=["TEST_POSIX_MQ", "HAS_SYS_MQ_OPEN"],
                     blocked_configure_flags=["ARM_HOST"]),
        ]

        tests.extend([
            TestSpec("ssh1", 4, ["./test/ssh1"],
                     pre_checkpoint_delay=1.5,
                     tags=["slow"],
                     configure_flags=["HAS_SSH_LOCALHOST"]),
            TestSpec("pthread_atfork1", 2,
                     ["./test/pthread_atfork1"],
                     library_paths=[f"{ROOT}/test"]),
            TestSpec("pthread_atfork2", 2,
                     ["./test/pthread_atfork2"],
                     library_paths=[f"{ROOT}/test"]),
            TestSpec("waitpid", 2, ["./test/waitpid"],
                     pre_checkpoint_delay=1.0,
                     tags=["slow"]),
            TestSpec("waitid-syscall", 1, ["./test/waitid-syscall"],
                     pre_checkpoint_delay=1.0,
                     tags=["slow"]),
            TestSpec("gzip", 1, ["./test/dmtcp1"],
                     env={"DMTCP_GZIP": "1"}),
            TestSpec("readline", 1, ["./test/readline"]),
            TestSpec("perl", 1, ["/usr/bin/perl -e 'while (1) { sleep 1 }'"],
                     post_launch_delay=2.0,
                     blocked_configure_flags=["AARCH64_HOST"]),
            TestSpec("python", 1,
                     [f"{sys.executable} -c 'import time; time.sleep(60)'"],
                     post_launch_delay=2.0),
            TestSpec("bash", 2,
                     ["/bin/bash --norc -c 'ls; sleep 30; ls'"],
                     env={"DMTCP_GZIP": "1"},
                     post_launch_delay=2.0),
            TestSpec("dash", 2,
                     ["/bin/dash -c 'ls; sleep 30; ls'"],
                     env={"DMTCP_GZIP": "0", "ENV": None},
                     post_launch_delay=2.0),
            TestSpec("zsh", 2,
                     ["/bin/zsh -f -c 'ls; sleep 30; ls'"],
                     env={"DMTCP_GZIP": "0"},
                     post_launch_delay=2.0,
                     pre_checkpoint_delay=3.6,
                     tags=["slow"]),
            TestSpec("tcsh", 2,
                     ["/bin/tcsh -f -c 'ls; sleep 30; ls'"],
                     env={"DMTCP_GZIP": "1"}),
            TestSpec(
                "script", [2, 3, 4],
                [
                    "/usr/bin/script -f -c 'bash -c \"ls; sleep 30\"' "
                    "{workdir}/dmtcp-test-typescript.tmp"
                ],
                env={"SHELL": "/bin/bash"},
                post_launch_delay=2.0,
                pre_checkpoint_delay=2.1,
                tags=["slow"]),
            # Ubuntu 24.04 aarch64 vim is built with PAC and fails under
            # DMTCP; keep this PTY coverage enabled for non-AArch64 hosts.
            TestSpec(
                "vim", 1,
                [f"{getattr(autotest_config, 'VIM', 'vim')} "
                 "-X -u DEFAULTS -i NONE /etc/passwd +3"],
                env={"TERM": "vt100"},
                pre_checkpoint_delay=10.0,
                launch_mode="pty",
                tags=["slow", "pty"],
                requirements=["pty", "vim"],
                configure_flags=["HAS_VIM"],
                blocked_configure_flags=["AARCH64_HOST"]),
            TestSpec(
                "emacs", [1, 2],
                ["/usr/bin/emacs -nw -Q /etc/passwd"],
                env={"TERM": "vt100", "DMTCP_GZIP": "0"},
                pre_checkpoint_delay=12.0,
                launch_mode="pty",
                tags=["slow", "pty"],
                requirements=["pty", "emacs"]),
            TestSpec(
                "screen", 3,
                ["/usr/bin/screen -c /dev/null -s /bin/sh"],
                env={"TERM": "vt100"},
                # screen creates a Unix socket under SCREENDIR. Keep the
                # path short enough for sockaddr_un even in nested worktrees.
                private_env_dirs={
                    "SCREENDIR":
                    f"{tempfile.gettempdir()}/{{workname}}-screen",
                },
                pre_checkpoint_delay=0.9,
                launch_mode="pty",
                tags=["slow", "pty"],
                requirements=["pty", "screen"]),
            TestSpec("cilk1", 1, ["./test/cilk1 38"]),
            TestSpec(
                "matlab-nodisplay", 1,
                [f"{getattr(autotest_config, 'MATLAB', 'matlab')} "
                 "-nodisplay -nojvm"],
                pre_checkpoint_delay=3.0,
                tags=["slow"],
                configure_flags=["HAS_MATLAB"]),
            TestSpec("hellompich-n1", 3,
                     [f"{getattr(autotest_config, 'MPICH_PATH', '')}/mpirun "
                      "-np 1 ./test/hellompich"],
                     configure_flags=["HAS_MPICH"]),
            TestSpec("hellompich-n2", 4,
                     [f"{getattr(autotest_config, 'MPICH_PATH', '')}/mpirun "
                      "-np 2 ./test/hellompich"],
                     configure_flags=["HAS_MPICH"]),
            TestSpec(
                "openmpi", [5, 6],
                [f"{getattr(autotest_config, 'OPENMPI_MPIRUN', 'mpirun')} "
                 "-np 4 ./test/openmpi"],
                pre_checkpoint_delay=0.9,
                tags=["slow"],
                configure_flags=["HAS_OPENMPI"]),
            TestSpec("java1", 1, ["java -Xmx5M java1"],
                     env={"CLASSPATH": "./test"},
                     tags=["slow"],
                     configure_flags=["HAS_JAVA", "HAS_JAVAC"],
                     required_files=["test/java1.class"]),
            TestSpec("openmp-1", 1, ["./test/openmp-1"],
                     pre_checkpoint_delay=0.9,
                     blocked_configure_flags=["AARCH64_HOST"],
                     tags=["slow"]),
            TestSpec("openmp-2", 1, ["./test/openmp-2"],
                     pre_checkpoint_delay=0.9,
                     blocked_configure_flags=["AARCH64_HOST"],
                     tags=["slow"],
                     run_serial=True),
        ])

        return tests

    def get_test(self, name: str) -> TestSpec:
        for test in self._tests:
            if test.name == name:
                return test
        raise KeyError(name)

    @staticmethod
    def _has_all(values, required) -> bool:
        return all(value in values for value in required)

    def select(self, names=None, tags=None, requirements=None) -> List[TestSpec]:
        if names:
            tests = [self.get_test(name) for name in names]
        else:
            tests = list(self._tests)
        tags = tags or []
        requirements = requirements or []
        return [
            test for test in tests
            if self._has_all(test.tags, tags) and
            self._has_all(test.requirements, requirements)
        ]

    def select_for_listing(
            self, names=None, tags=None,
            requirements=None) -> List[TestSpec]:
        all_tests = list(self._tests) + list(self._disabled_tests)
        if names:
            by_name = {test.name: test for test in all_tests}
            tests = [by_name[name] for name in names]
        else:
            tests = all_tests
        tags = tags or []
        requirements = requirements or []
        return [
            test for test in tests
            if self._has_all(test.tags, tags) and
            self._has_all(test.requirements, requirements)
        ]

    def disabled_reason_counts(
            self, tags=None, requirements=None) -> Dict[str, int]:
        tags = tags or []
        requirements = requirements or []
        counts = Counter()
        for test in self._disabled_tests:
            if not self._has_all(test.tags, tags):
                continue
            if not self._has_all(test.requirements, requirements):
                continue
            reason = test.list_notes[0] if test.list_notes else "disabled"
            counts[reason] += 1
        return dict(counts)


REGISTRY = TestRegistry()

COMMAND_TESTS = (
    CommandTestSpec(
        "dmtcp-unit",
        "Unit tests",
        "unit",
        ["./unit/dmtcp_unit_tests"],
    ),
    CommandTestSpec(
        "autotest-unit",
        "Harness tests",
        "harness",
        [sys.executable, "./autotest_test.py"],
    ),
    CommandTestSpec(
        "coordinator-synthetic",
        COORDINATOR_PROTOCOL_CATEGORY,
        "coordinator",
        [sys.executable, "./coordinator_synthetic.py"],
    ),
    CommandTestSpec(
        "command-json",
        COORDINATOR_PROTOCOL_CATEGORY,
        "coordinator",
        [sys.executable, "./test_dmtcp_command_json.py"],
    ),
)
SUITE_ORDER = ("unit", "harness", "coordinator", "integration")
COMMAND_CATEGORY_ORDER = (
    "Unit tests",
    "Harness tests",
    COORDINATOR_PROTOCOL_CATEGORY,
)
COMMAND_INTEGRATION_CATEGORIES = frozenset({
    COORDINATOR_PROTOCOL_CATEGORY,
})


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("-v", "--verbose", action="store_true",
                        help="print per-test status")
    parser.add_argument("--list", action="store_true",
                        help="list tests known to the new harness")
    parser.add_argument("--retry-once", action="store_true",
                        help="retry a failing test once before reporting it")
    parser.add_argument("--jobs", type=positive_int, default=1,
                        help="run up to N checkpoint/restart integration "
                             "tests in parallel (default: 1)")
    parser.add_argument("--slow", action="count", default=0,
                        help="multiply checkpoint settle time and timeouts "
                             "by 5; repeat to multiply again")
    parser.add_argument("--retain-success-artifacts", action="store_true",
                        help="keep per-test artifact directories for "
                             "successful tests")
    parser.add_argument("--tag", action="append", default=[],
                        help="run or list tests with this metadata tag")
    parser.add_argument("--requires", action="append", default=[],
                        help="run or list tests with this requirement marker")
    parser.add_argument("--suite", action="append",
                        choices=["all", "unit", "harness", "coordinator",
                                 "integration"],
                        help="test suite to run; repeat to combine suites "
                             "(default: integration)")
    parser.add_argument("--color", choices=["auto", "always", "never"],
                        default="auto",
                        help="color failed statuses: auto, always, or never "
                             "(default: auto)")
    parser.add_argument("tests", nargs="*", metavar="TESTNAME",
                        help="test names to run")
    return parser.parse_args()


def positive_int(value: str) -> int:
    try:
        parsed = int(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError(
            f"expected a positive integer, got {value!r}") from error
    if parsed < 1:
        raise argparse.ArgumentTypeError(
            f"expected a positive integer, got {value!r}")
    return parsed


LIST_HEADER = ("Test",)
LIST_HEADER_WITH_NOTES = ("Test", "Notes")
LIST_NOTE_TAGS = frozenset({"slow", "pty"})
LIST_CATEGORY_ORDER = (
    "Single-process programs",
    "Thread and mutex tests",
    "Process control and signals",
    "Multi-process coordination",
    "IPC and shared resources",
    "PTY handling",
    "Interactive shells",
    "Terminal-oriented programs",
    "Checkpoint mechanics",
    COORDINATOR_PROTOCOL_CATEGORY,
    "Plugin behavior",
    "Language runtimes",
    "Parallel runtimes",
    "Build variants",
    "Logging",
    TestRegistry.DISABLED_CATEGORY,
)


def _list_notes(spec: TestSpec) -> str:
    if spec.list_notes:
        return ", ".join(spec.list_notes)
    notes = []
    if spec.cycles != 2:
        label = "checkpoint" if spec.cycles == 1 else "checkpoints"
        notes.append(f"{spec.cycles} {label}")
    notes.extend(tag for tag in spec.tags if tag in LIST_NOTE_TAGS)
    return ", ".join(notes)


def list_has_notes(specs: Iterable[TestSpec]) -> bool:
    return any(_list_notes(spec) for spec in specs)


def list_header(include_notes: bool):
    return LIST_HEADER_WITH_NOTES if include_notes else LIST_HEADER


def _list_entry_columns(entry, include_notes: bool):
    if isinstance(entry, TestSpec):
        columns = (entry.name,)
        if include_notes:
            columns += (_list_notes(entry),)
        return columns
    return tuple(str(value) for value in entry)


def list_column_widths(specs, include_notes: bool):
    rows = [_list_entry_columns(list_header(include_notes), include_notes)]
    rows.extend(_list_entry_columns(spec, include_notes) for spec in specs)
    return tuple(
        max(len(row[column]) for row in rows)
        for column in range(len(rows[0]))
    )


def format_list_entry(entry, widths, include_notes: bool, indent: str = ""):
    columns = _list_entry_columns(entry, include_notes)
    return indent + "  ".join(
        f"{columns[column]:<{widths[column]}}"
        for column in range(len(columns))
    ).rstrip()


def list_separator(widths):
    return "  ".join("-" * width for width in widths)


def list_groups(specs: Iterable[TestSpec]):
    groups: Dict[str, List[TestSpec]] = {}
    for spec in specs:
        groups.setdefault(spec.category, []).append(spec)
    category_rank = {
        category: index for index, category in enumerate(LIST_CATEGORY_ORDER)
    }
    return [
        (category, sorted(tests, key=lambda spec: spec.name))
        for category, tests in sorted(
            groups.items(),
            key=lambda item: (
                category_rank.get(item[0], len(LIST_CATEGORY_ORDER)),
                item[0],
            ),
        )
    ]


def _failed_phase_label(phase: str) -> str:
    if phase == "checkpoint":
        return "ckpt"
    if phase == "restart":
        return "rstr"
    return "run"


def color_enabled() -> bool:
    if COLOR_MODE == "always":
        return True
    if COLOR_MODE == "never":
        return False
    if os.environ.get("NO_COLOR") is not None:
        return False
    if os.environ.get("TERM") == "dumb":
        return False
    return sys.stdout.isatty()


def red_text(text: str) -> str:
    if not color_enabled():
        return text
    return f"{COLOR_RED}{text}{COLOR_RESET}"


def section_underline(title: str) -> str:
    if title.startswith("=="):
        return "=" * len(title)
    return "-" * len(title)


def print_section_header(title: str):
    print(title, flush=True)
    print(section_underline(title), flush=True)


def _elapsed_suffix(elapsed: Optional[float]) -> str:
    if elapsed is None:
        return ""
    return f" ({elapsed:.1f}s)"


def format_failed_run_status(result: TestResult) -> str:
    return red_text(
        f"{_failed_phase_label(result.phase)}:FAILED "
        f"phase={result.phase} msg={result.message}"
    )


def format_active_failed_run_status(result: TestResult) -> str:
    return red_text(f"FAILED phase={result.phase} msg={result.message}")


def run_display_name(spec: Union[TestSpec, CommandTestSpec]) -> str:
    name = spec.name
    if spec.category == COORDINATOR_PROTOCOL_CATEGORY:
        return name.removeprefix("coordinator-")
    return name


def run_name_width(suites: Iterable[str],
                   selected: Iterable[TestSpec]) -> int:
    return RUN_TESTNAME_WIDTH


def format_run_entry(spec: Union[TestSpec, CommandTestSpec],
                     status: str,
                     name_width: int = RUN_TESTNAME_WIDTH) -> str:
    return format_name_entry(run_display_name(spec), status, name_width)


def format_name_entry(name: str,
                      status: str,
                      name_width: int = RUN_TESTNAME_WIDTH) -> str:
    if len(name) >= name_width:
        return f"{name} {status}"
    return f"{name:<{name_width}}{status}"


class IntegrationProgressPrinter:
    def __init__(self, name_width: int = RUN_TESTNAME_WIDTH):
        self.spec: Optional[TestSpec] = None
        self.row_started = False
        self.active_activity = False
        self.name_width = name_width

    def start_test(self, spec: TestSpec, verbose: bool):
        self.spec = spec
        self.row_started = False
        self.active_activity = False
        if verbose:
            print(
                f"  {format_run_entry(spec, 'starting', self.name_width)}",
                flush=True,
            )

    def _ensure_row(self):
        if self.spec is None:
            return
        if not self.row_started:
            sys.stdout.write(
                f"  {format_run_entry(self.spec, '', self.name_width)}"
            )
            self.row_started = True

    def event(self, event: str):
        if event == "cycle-separator":
            self._ensure_row()
            sys.stdout.write(" -> ")
        elif event == "ckpt-start":
            self._ensure_row()
            sys.stdout.write("ckpt:")
            self.active_activity = True
        elif event == "ckpt-passed":
            self._ensure_row()
            sys.stdout.write("PASSED; ")
            self.active_activity = False
        elif event == "rstr-start":
            self._ensure_row()
            sys.stdout.write("rstr:")
            self.active_activity = True
        elif event == "rstr-passed":
            self._ensure_row()
            sys.stdout.write("PASSED")
            self.active_activity = False
        elif event == "run-passed":
            self._ensure_row()
            sys.stdout.write("run:PASSED")
            self.active_activity = False
        sys.stdout.flush()

    def abort_result(self, result: TestResult):
        if self.row_started:
            if self.active_activity:
                sys.stdout.write(format_active_failed_run_status(result))
            else:
                sys.stdout.write(format_failed_run_status(result))
            sys.stdout.write("\n")
            sys.stdout.flush()
            self.row_started = False
            self.active_activity = False

    def finish_result(self, spec: TestSpec, result: TestResult,
                      elapsed: Optional[float] = None):
        elapsed_text = _elapsed_suffix(elapsed)
        if result.passed:
            if not self.row_started:
                self.start_test(spec, verbose=False)
                self.event("run-passed")
            sys.stdout.write(f"{elapsed_text}\n")
        elif self.row_started:
            if self.active_activity:
                sys.stdout.write(format_active_failed_run_status(result))
            else:
                sys.stdout.write(format_failed_run_status(result))
            sys.stdout.write(f"{elapsed_text}\n")
        else:
            status = format_failed_run_status(result)
            status = f"{status}{elapsed_text}"
            sys.stdout.write(
                f"  {format_run_entry(spec, status, self.name_width)}\n"
            )
        sys.stdout.flush()
        self.row_started = False
        self.active_activity = False


class IntegrationProgressRecorder:
    def __init__(self):
        self.row_started = False
        self.active_activity = False
        self.parts: List[str] = []

    def start_test(self, spec: TestSpec, verbose: bool):
        self.row_started = False
        self.active_activity = False
        self.parts = []

    def _ensure_row(self):
        self.row_started = True

    def event(self, event: str):
        if event == "cycle-separator":
            self._ensure_row()
            self.parts.append(" -> ")
        elif event == "ckpt-start":
            self._ensure_row()
            self.parts.append("ckpt:")
            self.active_activity = True
        elif event == "ckpt-passed":
            self._ensure_row()
            self.parts.append("PASSED; ")
            self.active_activity = False
        elif event == "rstr-start":
            self._ensure_row()
            self.parts.append("rstr:")
            self.active_activity = True
        elif event == "rstr-passed":
            self._ensure_row()
            self.parts.append("PASSED")
            self.active_activity = False
        elif event == "run-passed":
            self._ensure_row()
            self.parts.append("run:PASSED")
            self.active_activity = False

    def abort_result(self, result: TestResult):
        self.row_started = False
        self.active_activity = False
        self.parts = []

    def finish_result(self, result: TestResult) -> str:
        if result.passed:
            if not self.row_started:
                self.event("run-passed")
            return "".join(self.parts)
        if self.active_activity:
            self.parts.append(format_active_failed_run_status(result))
        elif self.row_started:
            self.parts.append(format_failed_run_status(result))
        else:
            self.parts.append(format_failed_run_status(result))
        return "".join(self.parts)


@dataclass(frozen=True)
class IntegrationRunRecord:
    spec: TestSpec
    result: TestResult
    elapsed: float
    status: str


def selected_suites(requested_suites: Optional[List[str]]) -> List[str]:
    if not requested_suites:
        return ["integration"]
    if "all" in requested_suites:
        return list(SUITE_ORDER)
    return [
        suite for suite in SUITE_ORDER
        if suite in requested_suites
    ]


def command_output_summary(output: str) -> str:
    lines = [line.strip() for line in output.splitlines() if line.strip()]
    for line in reversed(lines):
        if line.startswith("Ran "):
            return line
        if "unit tests passed" in line:
            return line
    return lines[-1] if lines else ""


def run_command_test(spec: CommandTestSpec) -> TestResult:
    try:
        result = subprocess.run(
            spec.command,
            cwd=str(spec.cwd),
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
    except OSError as error:
        return TestResult.fail(spec.name, "run", str(error))

    output = result.stdout
    if result.stderr:
        output = f"{output}\n{result.stderr}" if output else result.stderr
    summary = command_output_summary(output)
    if result.returncode == 0:
        return TestResult.pass_(spec.name, message=summary)
    message = f"exit={result.returncode}"
    if summary:
        message = f"{message} {summary}"
    return TestResult.fail(spec.name, "run", message, details=output)


def format_command_run_status(result: TestResult) -> str:
    if result.passed:
        status = "PASS."
        if result.message:
            status = f"{status} {result.message}"
        return status
    return red_text(f"FAIL. {result.message}")


def command_failure_detail_lines(
        spec: CommandTestSpec, result: TestResult,
        max_output_lines: int = COMMAND_OUTPUT_TAIL_LINES) -> List[str]:
    lines = [
        f"command: {shlex.join(spec.command)}",
        f"cwd: {spec.cwd}",
    ]
    output = result.details.rstrip()
    if output:
        tail = output.splitlines()[-max_output_lines:]
        lines.append(f"output tail (last {len(tail)} lines):")
        lines.extend(f"  {line}" for line in tail)
    return lines


def command_tests_for_suites(suites: Iterable[str]) -> List[CommandTestSpec]:
    suite_set = set(suites)
    return [test for test in COMMAND_TESTS if test.suite in suite_set]


def command_test_groups(command_tests: Iterable[CommandTestSpec]):
    groups: Dict[str, List[CommandTestSpec]] = {}
    for test in command_tests:
        groups.setdefault(test.category, []).append(test)
    category_rank = {
        category: index
        for index, category in enumerate(COMMAND_CATEGORY_ORDER)
    }
    return [
        (category, tests)
        for category, tests in sorted(
            groups.items(),
            key=lambda item: (
                category_rank.get(item[0], len(COMMAND_CATEGORY_ORDER)),
                item[0],
            ),
        )
    ]


def failure_records_from_result(
        spec: Union[TestSpec, CommandTestSpec],
        result: TestResult) -> FailureRecord:
    if isinstance(spec, CommandTestSpec):
        return FailureRecord(
            run_display_name(spec),
            result.phase,
            result.message,
            command=spec.command,
            cwd=spec.cwd,
        )
    return FailureRecord(
        run_display_name(spec),
        result.phase,
        result.message,
        artifact_dir=result.artifact_dir,
    )


def format_failure_record_lines(
        failure: FailureRecord,
        name_width: int = RUN_TESTNAME_WIDTH) -> List[str]:
    status = f"phase={failure.phase} msg={failure.message}"
    lines = [f"  {format_name_entry(failure.name, status, name_width)}"]
    if failure.artifact_dir is not None:
        lines.append(f"    artifacts: {failure.artifact_dir}")
        lines.append(f"    logs: {ARTIFACT_LOG_HINT}")
    if failure.command is not None:
        lines.append(f"    command: {shlex.join(failure.command)}")
    if failure.cwd is not None:
        lines.append(f"    cwd: {failure.cwd}")
    return lines


def print_failure_summary(
        failures: List[FailureRecord],
        name_width: int = RUN_TESTNAME_WIDTH):
    if not failures:
        return
    print(flush=True)
    print_section_header("== Failures ==")
    for failure in failures:
        for line in format_failure_record_lines(failure, name_width):
            print(line, flush=True)


def print_summary(passed: int, total: int, disabled_counts: Dict[str, int]):
    skipped = sum(disabled_counts.values())
    print_section_header("== Summary ==")
    print(
        f"test groups: pass={passed} fail={total - passed} "
        f"skipped={skipped} total={total + skipped}",
        flush=True,
    )
    if skipped:
        print("skipped by reason:", flush=True)
        for reason, count in sorted(disabled_counts.items()):
            print(f"  {count} {reason}", flush=True)
        print("run ./test/autotest.py --list for disabled-test details",
              flush=True)


def run_integration_specs(args, harness: DmtcpHarness,
                          progress: IntegrationProgressPrinter,
                          tests: List[TestSpec],
                          name_width: int
                          ) -> Tuple[int, int, List[FailureRecord]]:
    passed = 0
    failures = []
    for spec in tests:
        progress.start_test(spec, args.verbose)
        start_time = time.monotonic()
        result = run_with_optional_retry(harness, spec, args.retry_once)
        elapsed = time.monotonic() - start_time
        if result.passed:
            passed += 1
            progress.finish_result(spec, result, elapsed)
            if result.artifact_dir is not None:
                artifact_status = f"artifacts={result.artifact_dir}"
                row = format_run_entry(spec, artifact_status, name_width)
                print(f"  {row}", flush=True)
        else:
            failures.append(failure_records_from_result(spec, result))
            progress.finish_result(spec, result, elapsed)
            artifact_status = f"artifacts={result.artifact_dir}"
            row = format_run_entry(spec, artifact_status, name_width)
            print(f"  {row}", flush=True)
    return passed, len(tests), failures


def make_integration_harness(args) -> DmtcpHarness:
    return DmtcpHarness(
        verbose=args.verbose,
        retain_success_artifacts=args.retain_success_artifacts,
        slow_count=args.slow,
    )


def run_integration_spec_record(args, spec: TestSpec) -> IntegrationRunRecord:
    harness = make_integration_harness(args)
    progress = IntegrationProgressRecorder()
    harness.progress = progress.event
    harness.start_progress = progress.start_test
    harness.abort_progress = progress.abort_result
    start_time = time.monotonic()
    result = run_with_optional_retry(harness, spec, args.retry_once)
    elapsed = time.monotonic() - start_time
    status = progress.finish_result(result)
    return IntegrationRunRecord(spec, result, elapsed, status)


def print_integration_record(record: IntegrationRunRecord,
                             name_width: int):
    status = f"{record.status}{_elapsed_suffix(record.elapsed)}"
    row = format_run_entry(record.spec, status, name_width)
    print(f"  {row}", flush=True)
    if record.result.artifact_dir is not None:
        artifact_status = f"artifacts={record.result.artifact_dir}"
        row = format_run_entry(record.spec, artifact_status, name_width)
        print(f"  {row}", flush=True)


def run_integration_specs_parallel(args, tests: List[TestSpec],
                                   name_width: int
                                   ) -> Tuple[int, int, List[FailureRecord]]:
    passed = 0
    failures = []
    parallel_tests = [spec for spec in tests if not spec.run_serial]
    serial_tests = [spec for spec in tests if spec.run_serial]
    with concurrent.futures.ThreadPoolExecutor(
            max_workers=args.jobs) as executor:
        futures = {
            executor.submit(run_integration_spec_record, args, spec): spec
            for spec in parallel_tests
        }
        for future in concurrent.futures.as_completed(futures):
            spec = futures[future]
            try:
                record = future.result()
            except Exception as error:
                result = TestResult.fail(spec.name, "harness", str(error))
                record = IntegrationRunRecord(
                    spec,
                    result,
                    0.0,
                    format_failed_run_status(result),
                )
            if record.result.passed:
                passed += 1
            else:
                failures.append(failure_records_from_result(
                    record.spec, record.result))
            print_integration_record(record, name_width)
    for spec in serial_tests:
        record = run_integration_spec_record(args, spec)
        if record.result.passed:
            passed += 1
        else:
            failures.append(failure_records_from_result(
                record.spec, record.result))
        print_integration_record(record, name_width)
    return passed, len(tests), failures


def run_command_suites(suites: Iterable[str],
                       name_width: int = RUN_TESTNAME_WIDTH,
                       integration_by_category=None,
                       integration_runner=None
                       ) -> Tuple[int, int, List[FailureRecord]]:
    command_tests = command_tests_for_suites(suites)
    passed = 0
    total = 0
    failures = []
    for category, tests in command_test_groups(command_tests):
        print(flush=True)
        print_section_header(category)
        for test in tests:
            total += 1
            result = run_command_test(test)
            if result.passed:
                passed += 1
            else:
                failures.append(failure_records_from_result(test, result))
            status = format_command_run_status(result)
            print(f"  {format_run_entry(test, status, name_width)}",
                  flush=True)
            if not result.passed:
                for line in command_failure_detail_lines(test, result):
                    print(f"    {line}", flush=True)
        if (integration_by_category is not None and
                integration_runner is not None):
            integration_tests = integration_by_category.pop(category, [])
            integration_passed, integration_total, integration_failures = (
                integration_runner(integration_tests)
            )
            passed += integration_passed
            total += integration_total
            failures.extend(integration_failures)
    return passed, total, failures


def make_integration_runner(args, name_width: int = RUN_TESTNAME_WIDTH):
    harness = make_integration_harness(args)
    progress = IntegrationProgressPrinter(name_width)
    harness.progress = progress.event
    harness.start_progress = progress.start_test
    harness.abort_progress = progress.abort_result
    return harness, progress


def run_with_optional_retry(harness, spec, retry_once):
    result = harness.run(spec)
    if result.passed or not retry_once:
        return result

    abort_progress = getattr(harness, "abort_progress", None)
    if abort_progress is not None:
        abort_progress(result)
    print(f"{spec.name}: retrying after phase={result.phase} "
          f"msg={result.message} artifacts={result.artifact_dir}",
          flush=True)
    start_progress = getattr(harness, "start_progress", None)
    if start_progress is not None:
        start_progress(spec, False)
    retry_result = harness.run(spec)
    if not retry_result.passed:
        return retry_result

    print(f"{spec.name}: PASSED on retry", flush=True)
    return retry_result


def print_integration_list(selected: List[TestSpec]) -> int:
    include_notes = list_has_notes(selected)
    widths = list_column_widths(selected, include_notes)
    print(format_list_entry(list_header(include_notes), widths,
                            include_notes))
    print(list_separator(widths))
    for category, tests in list_groups(selected):
        print()
        print(category)
        for test in tests:
            print(format_list_entry(test, widths, include_notes,
                                    indent="  "))
    return 0


def run_integration_tests(
        args, selected: List[TestSpec],
        show_suite_heading: bool,
        name_width: int = RUN_TESTNAME_WIDTH) -> Tuple[int, int, List[FailureRecord]]:
    passed = 0
    failures = []
    if show_suite_heading:
        print(flush=True)
        print_section_header("Checkpoint/restart integration tests")
    for category, tests in list_groups(selected):
        print(flush=True)
        print_section_header(category)
        if args.jobs > 1:
            group_passed, _, group_failures = run_integration_specs_parallel(
                args, tests, name_width)
        else:
            harness, progress = make_integration_runner(args, name_width)
            group_passed, _, group_failures = run_integration_specs(
                args, harness, progress, tests, name_width)
        passed += group_passed
        failures.extend(group_failures)

    return passed, len(selected), failures


def main():
    global COLOR_MODE
    args = parse_args()
    COLOR_MODE = args.color
    suites = selected_suites(args.suite)

    try:
        if args.list:
            selected = REGISTRY.select_for_listing(
                args.tests, args.tag, args.requires)
            if not selected:
                print("No tests selected", file=sys.stderr)
                return 2
            return print_integration_list(selected)

        selected = []
        if "integration" in suites:
            selected = REGISTRY.select(args.tests, args.tag, args.requires)
        elif args.tests:
            print("Test names can only select integration tests",
                  file=sys.stderr)
            return 2
    except KeyError as error:
        print(f"Unknown test: {error.args[0]}", file=sys.stderr)
        return 2

    if "integration" in suites and not selected:
        print("No tests selected", file=sys.stderr)
        return 2

    passed = 0
    total = 0
    failures = []
    name_width = run_name_width(suites, selected)
    print_section_header("== Tests ==")
    selected_for_integration = selected
    integration_by_command_category = None
    integration_runner = None
    if "integration" in suites:
        command_categories = {
            category
            for category, _ in command_test_groups(
                command_tests_for_suites(suites))
        }
        consumed_categories = (
            command_categories & COMMAND_INTEGRATION_CATEGORIES
        )
        if consumed_categories:
            selected_for_integration = [
                spec for spec in selected
                if spec.category not in consumed_categories
            ]
            consumed_specs = [
                spec for spec in selected
                if spec.category in consumed_categories
            ]
            integration_by_command_category = {
                category: tests
                for category, tests in list_groups(consumed_specs)
            }
            if args.jobs > 1:
                integration_runner = (
                    lambda tests: run_integration_specs_parallel(
                        args, tests, name_width)
                )
            else:
                harness, progress = make_integration_runner(args, name_width)
                integration_runner = (
                    lambda tests: run_integration_specs(
                        args, harness, progress, tests, name_width)
                )

    command_passed, command_total, command_failures = run_command_suites(
        suites, name_width, integration_by_command_category,
        integration_runner)
    passed += command_passed
    total += command_total
    failures.extend(command_failures)
    if "integration" in suites and selected_for_integration:
        show_suite_heading = len(suites) > 1
        integration_passed, integration_total, integration_failures = run_integration_tests(
            args, selected_for_integration, show_suite_heading, name_width)
        passed += integration_passed
        total += integration_total
        failures.extend(integration_failures)

    disabled_counts = {}
    if "integration" in suites and not args.tests:
        disabled_counts = REGISTRY.disabled_reason_counts(
            args.tag, args.requires)
    print_summary(passed, total, disabled_counts)
    print_failure_summary(failures, name_width)
    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())
