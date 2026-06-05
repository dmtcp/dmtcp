#!/usr/bin/env python3

import gzip
import json
import os
import pathlib
import shlex
import shutil
import signal
import struct
import subprocess
import tempfile
import time
import traceback
from dataclasses import dataclass, field
from typing import Callable, Dict, List, Optional, Union


ROOT = pathlib.Path(__file__).resolve().parents[1]
DMTCP_CKPT_HEADER_SIZE = 4096
DMTCP_CKPT_SIGNATURE = b"DMTCP_CHECKPOINT_IMAGE_v5.0\n\0"
DMTCP_CKPT_HEADER_FORMAT_VERSION = 1
DMTCP_CKPT_ENDIAN_MARKER = 0x01020304
# Must match DmtcpCkptHeader::padding in include/dmtcp.h.
DMTCP_CKPT_HEADER_PADDING_OFFSET = 2320
DMTCP_CKPT_HEADER_PADDING_SIZE = 1776
DMTCP_COMMAND_JSON_SCHEMA_VERSION = 1


def validate_dmtcp_command_json_payload(payload: object):
    if not isinstance(payload, dict):
        raise ValueError("dmtcp_command JSON payload must be an object")
    version = payload.get("schema_version")
    if version != DMTCP_COMMAND_JSON_SCHEMA_VERSION:
        raise ValueError(
            f"unsupported dmtcp_command JSON schema: {version!r}"
        )


def parse_dmtcp_command_json(raw_output: str) -> Dict[str, object]:
    try:
        payload = json.loads(raw_output)
    except json.JSONDecodeError as error:
        raise ValueError(f"invalid JSON output: {error}") from error
    validate_dmtcp_command_json_payload(payload)
    return payload


def validate_dmtcp_command_result_payload(payload: Dict[str, object],
                                          expected_type: str,
                                          expected_phase: str):
    validate_dmtcp_command_json_payload(payload)
    actual_type = payload.get("type")
    if actual_type != expected_type:
        raise ValueError(
            f"expected JSON type '{expected_type}', got {actual_type!r}"
        )
    actual_phase = payload.get("phase")
    if actual_phase != expected_phase:
        raise ValueError(
            f"expected JSON phase '{expected_phase}', got {actual_phase!r}"
        )
    ok = payload.get("ok")
    if type(ok) is not bool:
        raise ValueError("dmtcp_command JSON field ok must be a boolean")
    if not ok:
        error_code = payload.get("error_code")
        if type(error_code) is not str:
            raise ValueError(
                "dmtcp_command JSON field error_code must be a string")
        error_message = payload.get("error_message")
        if type(error_message) is not str:
            raise ValueError(
                "dmtcp_command JSON field error_message must be a string")


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
        validate_dmtcp_command_json_payload(payload)
        if payload.get("type") != "status" or not payload.get("ok"):
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

    @staticmethod
    def pass_(name: str,
              artifact_dir: Optional[pathlib.Path] = None) -> "TestResult":
        return TestResult(name, True, "complete", "", artifact_dir)

    @staticmethod
    def fail(name: str, phase: str, message: str,
             artifact_dir: Optional[pathlib.Path] = None) -> "TestResult":
        return TestResult(name, False, phase, message, artifact_dir)


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
    validate_checkpoint_headers: bool = False
    expect_checkpoint_gzip: Optional[bool] = None
    completion_command: str = "--kill"
    coordinator_args: List[str] = field(default_factory=list)
    tags: List[str] = field(default_factory=list)
    requirements: List[str] = field(default_factory=list)
    limits: List[str] = field(default_factory=list)

    def peer_counts(self) -> List[int]:
        if isinstance(self.peers, int):
            return [self.peers]
        return list(self.peers)

    def checkpoint_kills_workers(self) -> bool:
        return self.checkpoint_command in ["--kcheckpoint", "-kc"]


def checkpoint_payload_succeeded(spec: TestSpec,
                                 payload: Dict[str, object]) -> bool:
    if payload.get("ok"):
        return True
    return (
        spec.checkpoint_kills_workers() and
        payload.get("error_code") == "not_running"
    )


def checkpoint_image_is_gzip(path: pathlib.Path) -> bool:
    with path.open("rb") as image:
        return image.read(2) == b"\x1f\x8b"


def validate_checkpoint_bootstrap_headers(path: pathlib.Path):
    required_size = 2 * DMTCP_CKPT_HEADER_SIZE
    if checkpoint_image_is_gzip(path):
        with gzip.open(path, "rb") as image:
            data = image.read(required_size)
    else:
        raw = path.read_bytes()
        data = raw[:required_size]
    if len(data) < required_size:
        raise HarnessFailure(
            "checkpoint-header",
            f"{path} is too small for two bootstrap records",
        )

    first = data[:DMTCP_CKPT_HEADER_SIZE]
    second = data[DMTCP_CKPT_HEADER_SIZE:required_size]
    if first != second:
        raise HarnessFailure(
            "checkpoint-header",
            f"{path} bootstrap records differ",
        )
    if not first.startswith(DMTCP_CKPT_SIGNATURE):
        raise HarnessFailure(
            "checkpoint-header",
            f"{path} has invalid checkpoint signature",
        )

    header_size, version, word_size, endian_marker = struct.unpack_from(
        "=IIII", first, 32)
    if header_size != DMTCP_CKPT_HEADER_SIZE:
        raise HarnessFailure(
            "checkpoint-header",
            f"{path} header_size={header_size}",
        )
    if version != DMTCP_CKPT_HEADER_FORMAT_VERSION:
        raise HarnessFailure(
            "checkpoint-header",
            f"{path} header_version={version}",
        )
    if word_size not in (4, 8):
        raise HarnessFailure(
            "checkpoint-header",
            f"{path} word_size={word_size}",
        )
    if endian_marker != DMTCP_CKPT_ENDIAN_MARKER:
        raise HarnessFailure(
            "checkpoint-header",
            f"{path} endian_marker=0x{endian_marker:x}",
        )
    padding = first[
        DMTCP_CKPT_HEADER_PADDING_OFFSET:
        DMTCP_CKPT_HEADER_PADDING_OFFSET + DMTCP_CKPT_HEADER_PADDING_SIZE
    ]
    if any(padding):
        raise HarnessFailure(
            "checkpoint-header",
            f"{path} has non-zero reserved padding",
        )


class DmtcpHarness:
    def __init__(self, root: pathlib.Path = ROOT, verbose: bool = False,
                 retain_success_artifacts: bool = False):
        self.root = root
        self.verbose = verbose
        self.retain_success_artifacts = retain_success_artifacts
        self.bin_dir = self.root / "bin"
        self.coordinator = self.bin_dir / "dmtcp_coordinator"
        self.command = self.bin_dir / "dmtcp_command"
        self.launch = self.bin_dir / "dmtcp_launch"
        self.restart = self.bin_dir / "dmtcp_restart"

    def run(self, spec: TestSpec) -> TestResult:
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
        self.env = self._make_env()
        self.coordinator_proc: Optional[subprocess.Popen] = None
        self.processes: List[subprocess.Popen] = []
        self.port: Optional[int] = None

    def run(self):
        self._verify_binaries()
        self._start_coordinator()
        self._assert_status(0, False, "initial-status")
        self._launch_processes()
        self._wait_for_status(self.spec.peer_counts(), True, "launch")
        if self.spec.post_launch_delay > 0.0:
            time.sleep(self.spec.post_launch_delay)
        for _ in range(self.spec.cycles):
            self._checkpoint()
            self._kill_workers()
            self._restart()
        self._complete_test()

    def cleanup(self):
        self._kill_workers(best_effort=True)
        self._quit_coordinator()
        for proc in self.processes:
            self._terminate_process_group(proc, f"worker {proc.pid}")
        if self.coordinator_proc is not None and self.coordinator_proc.poll() is None:
            self._terminate_process_group(self.coordinator_proc,
                                          f"coordinator {self.coordinator_proc.pid}")

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
                env[name] = value
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
            command_argv = shlex.split(command)
            if command_argv and command_argv[0].startswith("./test/"):
                executable = self.harness.root / command_argv[0]
                if not executable.exists():
                    raise HarnessFailure("setup", f"missing test binary: {command_argv[0]}")
            argv = [str(self.harness.launch), *command_argv]
            self._record_command(f"launch-worker-{index}", argv)
            stdout = open(self.work.path / f"worker-{index}.out", "w",
                          encoding="utf-8")
            proc = subprocess.Popen(
                argv,
                cwd=str(self.harness.root),
                env=self.env,
                text=True,
                stdin=subprocess.PIPE,
                stdout=stdout,
                stderr=subprocess.STDOUT,
                preexec_fn=os.setpgrp,
            )
            stdout.close()
            self.processes.append(proc)

    def _status(self) -> DmtcpStatus:
        payload = self._run_json_command("--status", "status", allow_error=False)
        try:
            return DmtcpStatus.from_json(payload)
        except ValueError as error:
            raise HarnessFailure("status", str(error)) from error

    def _checkpoint(self):
        if self.spec.pre_checkpoint_delay > 0.0:
            time.sleep(self.spec.pre_checkpoint_delay)
        payload = self._run_json_command(self.spec.checkpoint_command,
                                         "checkpoint",
                                         allow_error=(
                                             self.spec.checkpoint_kills_workers()
                                         ))
        if not checkpoint_payload_succeeded(self.spec, payload):
            raise HarnessFailure("checkpoint", str(payload.get("error_message")))
        self._wait_for(lambda: bool(self._checkpoint_images()),
                       "checkpoint", "checkpoint image was not created")
        images = self._checkpoint_images()
        self._record_checkpoint_images("checkpoint", images)
        if self.spec.expect_checkpoint_gzip is not None:
            for image in images:
                actual = checkpoint_image_is_gzip(image)
                if actual != self.spec.expect_checkpoint_gzip:
                    raise HarnessFailure(
                        "checkpoint-image",
                        f"{image} gzip={actual}; expected "
                        f"{self.spec.expect_checkpoint_gzip}",
                    )
        if self.spec.validate_checkpoint_headers:
            for image in images:
                validate_checkpoint_bootstrap_headers(image)
        if self.spec.checkpoint_kills_workers():
            self._wait_for_status(0, False, "checkpoint")
        else:
            self._wait_for_status(self.spec.peer_counts(), True, "checkpoint")

    def _kill_workers(self, best_effort: bool = False):
        try:
            self._run_json_command("--kill", "kill", allow_error=False)
            self._wait_for_status(0, False, "kill")
        except HarnessFailure:
            if not best_effort:
                raise

    def _quit_workers_and_coordinator(self):
        self._run_json_command("--quit", "quit", allow_error=False)
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
        self._run_json_command("--kill", "kill", allow_error=False)
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
        stdout = open(self.work.path / f"restart-{index}.out", "w",
                      encoding="utf-8")
        restart_args = [str(self.harness.restart), "--quiet"]
        if self.spec.restart_uses_directory:
            restart_args.extend(["--restartdir", str(self.work.ckpt_dir)])
        else:
            restart_args.extend([str(path) for path in images])
        self._record_command(f"restart-worker-{index}", restart_args)
        proc = subprocess.Popen(
            restart_args,
            cwd=str(self.harness.root),
            env=self.env,
            text=True,
            stdin=subprocess.PIPE,
            stdout=stdout,
            stderr=subprocess.STDOUT,
            preexec_fn=os.setpgrp,
        )
        stdout.close()
        self.processes.append(proc)
        self._wait_for_status(self.spec.peer_counts(), True, "restart")
        self._clear_checkpoint_dir()

    def _quit_coordinator(self):
        if self.coordinator_proc is None or self.coordinator_proc.poll() is not None:
            return
        try:
            self._run_json_command("--quit", "quit", allow_error=True)
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
                out.write(f"{image}\tgzip={checkpoint_image_is_gzip(image)}\n")

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
                          allow_error: bool) -> Dict[str, object]:
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
                f"dmtcp_command --json {command} timed out after "
                f"{self.spec.timeout} seconds",
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
            payload = parse_dmtcp_command_json(result.stdout)
        except ValueError as error:
            raise HarnessFailure(phase, str(error))
        try:
            validate_dmtcp_command_result_payload(payload, phase, phase)
        except ValueError as error:
            raise HarnessFailure(phase, str(error))
        if (result.returncode != 0 or not payload.get("ok")) and not allow_error:
            message = payload.get("error_message", result.stderr)
            raise HarnessFailure(phase, str(message))
        return payload

    def _assert_status(self, peers: Union[int, List[int]], running: bool,
                       phase: str):
        status = self._status()
        peer_counts = [peers] if isinstance(peers, int) else peers
        if status.num_peers not in peer_counts or status.running != running:
            raise HarnessFailure(
                phase,
                f"expected peers={peers} running={running}; "
                f"got peers={status.num_peers} running={status.running}",
            )

    def _wait_for_status(self, peers: Union[int, List[int]], running: bool,
                         phase: str):
        peer_counts = [peers] if isinstance(peers, int) else peers

        def matches_status() -> bool:
            status = self._status()
            return status.num_peers in peer_counts and status.running == running

        self._wait_for(matches_status, phase,
                       f"timed out waiting for peers={peers} running={running}")

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
        raise HarnessFailure(phase, message)

    def _checkpoint_images(self) -> List[pathlib.Path]:
        return sorted(self.work.ckpt_dir.glob("ckpt_*.dmtcp"))

    def _clear_checkpoint_dir(self):
        for path in self.work.ckpt_dir.iterdir():
            if path.is_dir():
                shutil.rmtree(path)
            else:
                path.unlink()
