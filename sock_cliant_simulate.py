#!/usr/bin/env python3

import argparse
import csv
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
    if not values:
        print(f"{name}: no data")
        return

    print()
    print(name)
    print("-" * len(name))
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
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "count",
        type=int,
        help="number of concurrent clients",
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
        "--timeout",
        type=float,
        default=10.0,
        help="socket timeout in seconds",
    )

    parser.add_argument(
        "--hold",
        type=float,
        default=0.0,
        help="seconds to keep all successful connections open after test",
    )

    parser.add_argument(
        "--csv",
        dest="csv_path",
        help="write per-client results to CSV",
    )

    parser.add_argument(
        "--quiet",
        action="store_true",
        help="suppress per-client OK messages",
    )

    args = parser.parse_args()

    count = args.count

    # 全threadをほぼ同時にconnect()へ進ませる
    connect_start_barrier = threading.Barrier(count + 1)

    # 全connect試行終了を待つ
    connect_cond = threading.Condition()
    connect_done = 0

    # 全connect終了後に、一斉にecho試験開始
    echo_start_event = threading.Event()

    # 全echo試行終了を待つ
    echo_cond = threading.Condition()
    echo_done = 0

    # 計測終了まで接続を維持
    release_event = threading.Event()

    result_lock = threading.Lock()

    results = []

    connected = 0
    echo_ok = 0

    def client_main(client_id):
        nonlocal connect_done
        nonlocal echo_done
        nonlocal connected
        nonlocal echo_ok

        result = {
            "client": client_id,
            "connect_ok": False,
            "echo_ok": False,
            "connect_ms": None,
            "echo_ms": None,
            "error": "",
        }

        sock = socket.socket(
            socket.AF_INET,
            socket.SOCK_STREAM,
        )

        sock.settimeout(args.timeout)

        try:
            #
            # Phase 1: connect
            #
            connect_start_barrier.wait()

            start_ns = time.perf_counter_ns()

            try:
                sock.connect((args.host, args.port))
            except Exception as e:
                end_ns = time.perf_counter_ns()

                result["connect_ms"] = (
                    end_ns - start_ns
                ) / 1_000_000

                result["error"] = f"connect: {e}"

                with result_lock:
                    results.append(result)

                print(
                    f"[CONNECT FAIL] client={client_id}: {e}"
                )

                with connect_cond:
                    connect_done += 1
                    connect_cond.notify_all()

                return

            end_ns = time.perf_counter_ns()

            result["connect_ms"] = (
                end_ns - start_ns
            ) / 1_000_000

            result["connect_ok"] = True

            with result_lock:
                connected += 1

            with connect_cond:
                connect_done += 1
                connect_cond.notify_all()

            #
            # 全clientのconnect試行終了までsocketを保持する
            #
            echo_start_event.wait()

            #
            # Phase 2: echo RTT
            #
            message = f"client-{client_id:04d}\n".encode()

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

                result["echo_ms"] = (
                    echo_end_ns - echo_start_ns
                ) / 1_000_000

                if received != message:
                    raise RuntimeError(
                        f"echo mismatch: "
                        f"expected={message!r}, "
                        f"actual={received!r}"
                    )

                result["echo_ok"] = True

                with result_lock:
                    echo_ok += 1

                if not args.quiet:
                    print(
                        f"[OK] client={client_id} "
                        f"connect={result['connect_ms']:.3f} ms "
                        f"echo={result['echo_ms']:.3f} ms"
                    )

            except Exception as e:
                echo_end_ns = time.perf_counter_ns()

                result["echo_ms"] = (
                    echo_end_ns - echo_start_ns
                ) / 1_000_000

                result["error"] = f"echo: {e}"

                print(
                    f"[ECHO FAIL] client={client_id}: {e}"
                )

            finally:
                with result_lock:
                    results.append(result)

                with echo_cond:
                    echo_done += 1
                    echo_cond.notify_all()

            #
            # 全echo試験終了までconnectionを保持
            #
            release_event.wait()

        finally:
            sock.close()

    threads = []

    for i in range(count):
        thread = threading.Thread(
            target=client_main,
            args=(i,),
        )
        thread.start()
        threads.append(thread)

    print(f"starting {count} clients...")

    #
    # Phase 1
    #
    connect_phase_start_ns = time.perf_counter_ns()

    connect_start_barrier.wait()

    with connect_cond:
        while connect_done < count:
            connect_cond.wait()

    connect_phase_end_ns = time.perf_counter_ns()

    with result_lock:
        connected_count = connected

    #
    # Phase 2
    #
    echo_phase_start_ns = time.perf_counter_ns()

    echo_start_event.set()

    with echo_cond:
        while echo_done < connected_count:
            echo_cond.wait()

    echo_phase_end_ns = time.perf_counter_ns()

    connect_phase_ms = (
        connect_phase_end_ns - connect_phase_start_ns
    ) / 1_000_000

    echo_phase_ms = (
        echo_phase_end_ns - echo_phase_start_ns
    ) / 1_000_000

    with result_lock:
        final_results = list(results)
        final_connected = connected
        final_echo_ok = echo_ok

    connect_values = [
        r["connect_ms"]
        for r in final_results
        if r["connect_ok"]
        and r["connect_ms"] is not None
    ]

    echo_values = [
        r["echo_ms"]
        for r in final_results
        if r["echo_ok"]
        and r["echo_ms"] is not None
    ]

    print()
    print("===== SUMMARY =====")
    print(f"requested       = {count}")
    print(f"connected       = {final_connected}")
    print(f"echo_ok         = {final_echo_ok}")
    print(
        f"connect_failed  = "
        f"{count - final_connected}"
    )
    print(
        f"echo_failed     = "
        f"{final_connected - final_echo_ok}"
    )

    print()
    print(f"connect phase   = {connect_phase_ms:.3f} ms")
    print(f"echo phase      = {echo_phase_ms:.3f} ms")

    if connect_phase_ms > 0:
        print(
            f"connect rate    = "
            f"{final_connected / (connect_phase_ms / 1000):.2f} conn/s"
        )

    if echo_phase_ms > 0:
        print(
            f"echo rate       = "
            f"{final_echo_ok / (echo_phase_ms / 1000):.2f} echo/s"
        )

    print_stats(
        "Connect latency",
        connect_values,
    )

    print_stats(
        "Echo RTT",
        echo_values,
    )

    #
    # 必要なら全connectionを少し保持
    #
    if args.hold > 0:
        print()
        print(
            f"holding connections for {args.hold:.1f} sec..."
        )
        time.sleep(args.hold)

    release_event.set()

    for thread in threads:
        thread.join()

    #
    # 生データ保存
    #
    if args.csv_path:
        final_results.sort(
            key=lambda r: r["client"]
        )

        with open(
            args.csv_path,
            "w",
            newline="",
        ) as f:
            writer = csv.DictWriter(
                f,
                fieldnames=[
                    "client",
                    "connect_ok",
                    "echo_ok",
                    "connect_ms",
                    "echo_ms",
                    "error",
                ],
            )

            writer.writeheader()
            writer.writerows(final_results)

        print()
        print(f"CSV written: {args.csv_path}")


if __name__ == "__main__":
    main()