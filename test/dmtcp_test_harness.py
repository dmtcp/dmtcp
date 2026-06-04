#!/usr/bin/env python3

import json
import os
import pathlib
import shlex
import shutil
import subprocess
import tempfile
import time
import traceback
from dataclasses import dataclass, field
from typing import Callable, Dict, List, Optional, Union


ROOT = pathlib.Path(__file__).resolve().parents[1]


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
        if payload.get("schema_version") != 1:
            raise ValueError("unsupported dmtcp_command JSON schema")
        if payload.get("type") != "status" or not payload.get("ok"):
            raise ValueError("JSON payload is not a successful status result")
        return DmtcpStatus(
            num_peers=int(payload["num_peers"]),
            running=bool(payload["running"]),
            checkpoint_interval=int(payload["checkpoint_interval"]),
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
            context.cleanup()
            if result is not None and result.passed and not self.retain_success_artifacts:
                work.cleanup()
        return result


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
        self._kill_workers()

    def cleanup(self):
        self._kill_workers(best_effort=True)
        self._quit_coordinator()
        for proc in self.processes:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait(timeout=5)
        if self.coordinator_proc is not None and self.coordinator_proc.poll() is None:
            self.coordinator_proc.terminate()
            try:
                self.coordinator_proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.coordinator_proc.kill()
                self.coordinator_proc.wait(timeout=5)

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
        self.coordinator_proc = subprocess.Popen(
            [
                str(self.harness.coordinator),
                "--quiet",
                "--coord-port",
                "0",
                "--port-file",
                str(self.work.port_file),
                "--timeout",
                "10800",
            ],
            cwd=str(self.harness.root),
            env=self.env,
            text=True,
            stdout=stdout,
            stderr=stderr,
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
            )
            stdout.close()
            self.processes.append(proc)

    def _status(self) -> DmtcpStatus:
        payload = self._run_json_command("--status", "status", allow_error=False)
        return DmtcpStatus.from_json(payload)

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

    def _restart(self):
        images = self._checkpoint_images()
        if not images:
            raise HarnessFailure("restart", "no checkpoint image available")
        index = len(self.processes)
        stdout = open(self.work.path / f"restart-{index}.out", "w",
                      encoding="utf-8")
        restart_args = [str(self.harness.restart), "--quiet"]
        if self.spec.restart_uses_directory:
            restart_args.extend(["--restartdir", str(self.work.ckpt_dir)])
        else:
            restart_args.extend([str(path) for path in images])
        proc = subprocess.Popen(
            restart_args,
            cwd=str(self.harness.root),
            env=self.env,
            text=True,
            stdin=subprocess.PIPE,
            stdout=stdout,
            stderr=subprocess.STDOUT,
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
            payload = json.loads(result.stdout)
        except json.JSONDecodeError as error:
            raise HarnessFailure(phase, f"invalid JSON output: {error}")
        if result.returncode != 0 and not allow_error:
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
