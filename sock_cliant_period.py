#!/usr/bin/env python3

import argparse
import socket
import statistics
import threading
import time


HOST = "192.0.2.2"
PORT = 7


def percentile(values, q):
    if not values:
        return None

    values = sorted(values)

    if len(values) == 1:
        return values[0]

    pos = (len(values) - 1) * q
    lower = int(pos)
    upper = min(lower + 1, len(values) - 1)
    ratio = pos - lower

    return values[lower] * (1 - ratio) + values[upper] * ratio


def print_stats(name, values):
    print()
    print(f"## {name}")

    if not values:
        print("no data")
        return

    print(f"count   = {len(values)}")
    print(f"avg     = {statistics.mean(values):.3f} ms")
    print(f"median  = {statistics.median(values):.3f} ms")
    print(f"p95     = {percentile(values, 0.95):.3f} ms")
    print(f"p99     = {percentile(values, 0.99):.3f} ms")
    print(f"min     = {min(values):.3f} ms")
    print(f"max     = {max(values):.3f} ms")

    if len(values) >= 2:
        print(f"stdev   = {statistics.stdev(values):.3f} ms")


def main():
    parser = argparse.ArgumentParser(
        description="TCP connection churn test"
    )

    parser.add_argument(
        "--workers",
        type=int,
        default=1,
        help="number of concurrent workers",
    )

    parser.add_argument(
        "--cycles",
        type=int,
        default=100,
        help="connections per worker",
    )

    parser.add_argument(
        "--interval",
        type=float,
        default=0.0,
        help="sleep seconds between connections in each worker",
    )

    parser.add_argument(
        "--timeout",
        type=float,
        default=5.0,
        help="socket timeout in seconds",
    )

    parser.add_argument(
        "--host",
        default=HOST,
    )

    parser.add_argument(
        "--port",
        type=int,
        default=PORT,
    )

    parser.add_argument(
        "--quiet",
        action="store_true",
        help="suppress successful per-connection logs",
    )

    args = parser.parse_args()

    total_attempts = args.workers * args.cycles

    lock = threading.Lock()
    start_barrier = threading.Barrier(args.workers + 1)

    connect_ok = 0
    connect_failed = 0

    echo_ok = 0
    echo_failed = 0

    connect_times = []
    echo_times = []

    first_failure = None

    def worker_main(worker_id):
        nonlocal connect_ok
        nonlocal connect_failed
        nonlocal echo_ok
        nonlocal echo_failed
        nonlocal first_failure

        start_barrier.wait()

        for cycle in range(args.cycles):
            sock = socket.socket(
                socket.AF_INET,
                socket.SOCK_STREAM,
            )
            sock.settimeout(args.timeout)

            connect_start_ns = time.perf_counter_ns()

            try:
                sock.connect((args.host, args.port))

                connect_end_ns = time.perf_counter_ns()
                connect_ms = (
                    connect_end_ns - connect_start_ns
                ) / 1_000_000

                with lock:
                    connect_ok += 1
                    connect_times.append(connect_ms)

            except Exception as e:
                connect_end_ns = time.perf_counter_ns()
                connect_ms = (
                    connect_end_ns - connect_start_ns
                ) / 1_000_000

                with lock:
                    connect_failed += 1

                    if first_failure is None:
                        first_failure = (
                            worker_id,
                            cycle,
                            "connect",
                            str(e),
                        )

                print(
                    f"[CONNECT FAIL] "
                    f"worker={worker_id} "
                    f"cycle={cycle} "
                    f"time={connect_ms:.3f} ms "
                    f"error={e}"
                )

                sock.close()

                if args.interval > 0:
                    time.sleep(args.interval)

                continue

            message = (
                f"worker={worker_id:03d} "
                f"cycle={cycle:06d}\n"
            ).encode()

            echo_start_ns = time.perf_counter_ns()

            try:
                sock.sendall(message)

                received = b""

                while len(received) < len(message):
                    chunk = sock.recv(
                        len(message) - len(received)
                    )

                    if not chunk:
                        raise RuntimeError(
                            "connection closed before full echo"
                        )

                    received += chunk

                echo_end_ns = time.perf_counter_ns()
                echo_ms = (
                    echo_end_ns - echo_start_ns
                ) / 1_000_000

                if received != message:
                    raise RuntimeError(
                        f"echo mismatch: "
                        f"expected={message!r}, "
                        f"actual={received!r}"
                    )

                with lock:
                    echo_ok += 1
                    echo_times.append(echo_ms)

                if not args.quiet:
                    print(
                        f"[OK] "
                        f"worker={worker_id} "
                        f"cycle={cycle} "
                        f"connect={connect_ms:.3f} ms "
                        f"echo={echo_ms:.3f} ms"
                    )

            except Exception as e:
                echo_end_ns = time.perf_counter_ns()
                echo_ms = (
                    echo_end_ns - echo_start_ns
                ) / 1_000_000

                with lock:
                    echo_failed += 1

                    if first_failure is None:
                        first_failure = (
                            worker_id,
                            cycle,
                            "echo",
                            str(e),
                        )

                print(
                    f"[ECHO FAIL] "
                    f"worker={worker_id} "
                    f"cycle={cycle} "
                    f"time={echo_ms:.3f} ms "
                    f"error={e}"
                )

            finally:
                #
                # 毎回明示的にclose
                #
                sock.close()

            if args.interval > 0:
                time.sleep(args.interval)

    threads = []

    for worker_id in range(args.workers):
        thread = threading.Thread(
            target=worker_main,
            args=(worker_id,),
        )
        thread.start()
        threads.append(thread)

    print(
        f"starting churn test: "
        f"workers={args.workers}, "
        f"cycles={args.cycles}, "
        f"total={total_attempts}"
    )

    test_start_ns = time.perf_counter_ns()

    start_barrier.wait()

    for thread in threads:
        thread.join()

    test_end_ns = time.perf_counter_ns()

    total_ms = (
        test_end_ns - test_start_ns
    ) / 1_000_000

    print()
    print("===== SUMMARY =====")
    print(f"workers          = {args.workers}")
    print(f"cycles/worker    = {args.cycles}")
    print(f"total attempts   = {total_attempts}")
    print()
    print(f"connect_ok       = {connect_ok}")
    print(f"connect_failed   = {connect_failed}")
    print(f"echo_ok          = {echo_ok}")
    print(f"echo_failed      = {echo_failed}")
    print()
    print(f"total time       = {total_ms:.3f} ms")

    slow_100 = sum(x >= 100 for x in connect_times)
    slow_500 = sum(x >= 500 for x in connect_times)
    slow_900 = sum(x >= 900 for x in connect_times)

    print()
    print("## Slow connects")
    print(
        f">= 100 ms = {slow_100} / {len(connect_times)} "
        f"({slow_100 / len(connect_times) * 100:.2f}%)"
    )
    print(
        f">= 500 ms = {slow_500} / {len(connect_times)} "
        f"({slow_500 / len(connect_times) * 100:.2f}%)"
    )
    print(
        f">= 900 ms = {slow_900} / {len(connect_times)} "
        f"({slow_900 / len(connect_times) * 100:.2f}%)"
    )

    if total_ms > 0:
        print(
            f"attempt rate     = "
            f"{total_attempts / (total_ms / 1000):.2f} conn/s"
        )

    if total_attempts > 0:
        print(
            f"connect success  = "
            f"{connect_ok / total_attempts * 100:.2f}%"
        )

    if connect_ok > 0:
        print(
            f"echo success     = "
            f"{echo_ok / connect_ok * 100:.2f}%"
        )

    if first_failure:
        print()
        print(
            "first failure    = "
            f"worker={first_failure[0]}, "
            f"cycle={first_failure[1]}, "
            f"phase={first_failure[2]}, "
            f"error={first_failure[3]}"
        )

    print_stats(
        "Connect latency",
        connect_times,
    )

    print_stats(
        "Echo RTT",
        echo_times,
    )


if __name__ == "__main__":
    main()