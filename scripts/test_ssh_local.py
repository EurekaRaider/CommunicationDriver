"""Run the native integration suite against an isolated loopback OpenSSH server."""

import argparse, getpass, os, subprocess, threading, sys, re
from collections import deque
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--build", default="build")
parser.add_argument(
    "--user-server", action="store_true", help="Run sshd as the current user without sudo"
)
parser.add_argument("--repeat", type=int, default=1)
parser.add_argument("--soak-seconds", type=int, default=0)
parser.add_argument("--output", type=Path, default=Path("artifacts/ssh-soak"))
parser.add_argument("--port", type=int, default=22482)
parser.add_argument("--rekey-limit")
parser.add_argument("--key-exchange", choices=["curve25519-sha256", "ecdh-sha2-nistp256"])

args = parser.parse_args()

if args.repeat < 1 or args.soak_seconds < 0:
    parser.error("repeat must be positive and soak seconds nonnegative")

if not 1 <= args.port <= 65535 or (
    args.rekey_limit and not re.fullmatch(r"[1-9][0-9]*[KMG]?", args.rekey_limit)
):
    parser.error("invalid port or rekey limit")

if args.port != 22482 and not args.soak_seconds:
    parser.error("custom port requires soak mode")

root = Path(__file__).resolve().parents[1]

if args.soak_seconds and not args.output.resolve().is_relative_to(root / "artifacts"):
    parser.error("soak output must be under ignored artifacts")

if args.soak_seconds:
    args.output.mkdir(parents=True, exist_ok=True)

server_log = (args.output / "sshd.log").open("w") if args.soak_seconds else None
fixture = root / "artifacts" / "ssh-fixture"
fixture.mkdir(parents=True, exist_ok=True)

for name in ["host_key", "client_key"]:
    key = fixture / name
    if not key.exists():
        subprocess.check_call(
            [
                "ssh-keygen",
                "-q",
                "-t",
                "rsa",
                "-b",
                "3072",
                "-m",
                "PEM",
                "-N",
                "",
                "-C",
                "CommunicationDriver-test",
                "-f",
                str(key),
            ]
        )

configuration = fixture / ("sshd_config-" + str(args.port))
pam = "yes" if os.uname().sysname == "Linux" else "no"
configuration.write_text(
    "Port "
    + str(args.port)
    + "\nListenAddress 127.0.0.1\nHostKey "
    + str(fixture / "host_key")
    + "\nAuthorizedKeysFile "
    + str(fixture / "client_key.pub")
    + "\nPidFile "
    + str(fixture / ("sshd-" + str(args.port) + ".pid"))
    + "\nPasswordAuthentication no\nKbdInteractiveAuthentication no\nUsePAM "
    + pam
    + "\nStrictModes no\nSubsystem sftp internal-sftp\nMaxStartups 128\nLogLevel INFO\n"
    + ("RekeyLimit " + args.rekey_limit + "\n" if args.rekey_limit else "")
    + ("KexAlgorithms " + args.key_exchange + "\n" if args.key_exchange else "")
)
(
    subprocess.run(["sudo", "-n", "mkdir", "-p", "/run/sshd"], check=True)
    if os.uname().sysname == "Linux" and not args.user_server
    else None
)

server = subprocess.Popen(
    ([] if args.user_server else ["sudo", "-n"])
    + ["/usr/sbin/sshd", "-D", "-e", "-f", str(configuration)],
    stderr=subprocess.PIPE,
    text=True,
)


def stop():
    if server.poll() is None:
        if args.user_server:
            server.terminate()
        else:
            subprocess.run(["sudo", "-n", "kill", "-TERM", str(server.pid)], check=False)


watchdog = threading.Timer(15, stop)
watchdog.start()

passed = False

try:
    startup = []

    for line in server.stderr:
        startup.append(line)
        if server_log:
            server_log.write(line)
            server_log.flush()
        if "Server listening on" in line:
            break
    else:
        raise RuntimeError("Isolated OpenSSH server failed to start: " + "".join(startup))

    watchdog.cancel()

    messages = deque(maxlen=100)

    def drain():
        for message in server.stderr:
            messages.append(message)
            if server_log:
                server_log.write(message)
                server_log.flush()

    reader = threading.Thread(target=drain, daemon=True)
    reader.start()

    environment = os.environ.copy()
    environment["CD_SSH_PORT"] = str(args.port)
    environment["CD_SSH_USER"] = getpass.getuser()
    environment["CD_SSH_PRIVATE_KEY"] = str(fixture / "client_key")
    environment["CD_SSH_FINGERPRINT"] = subprocess.check_output(
        ["ssh-keygen", "-lf", str(fixture / "host_key.pub"), "-E", "sha256"], text=True
    ).split()[1]

    if args.soak_seconds:
        command = [
            sys.executable,
            str(root / "scripts/run_stress.py"),
            "--output",
            str(args.output / "run.json"),
            "--timeout",
            str(args.soak_seconds + 300),
            "--idle-timeout",
            "120",
            "--interval",
            "5",
            "--process-tree",
            "--library-dir",
            str((root / args.build).resolve()),
            "--",
            str(root / args.build / "cd_transport_soak_tests"),
            "ssh",
            str(args.soak_seconds),
            str((args.output / "files").resolve()),
            "32",
        ]
        subprocess.run(command, env=environment, check=True, timeout=args.soak_seconds + 330)
    else:
        for _ in range(args.repeat):
            subprocess.run(
                [str(root / args.build / "cd_integration_tests"), "--ssh"],
                env=environment,
                check=True,
                timeout=120,
            )

    passed = True
finally:
    watchdog.cancel()
    stop()
    server.wait(timeout=10)

    if "reader" in locals():
        reader.join(timeout=5)

    if server_log:
        server_log.close()

    if not passed:
        print("".join(messages) if "messages" in locals() else server.stderr.read())
