#!/usr/bin/env python3
import argparse
import os
import re
import statistics
import subprocess
from pathlib import Path

try:
    import matplotlib.pyplot as plt
except ModuleNotFoundError:
    plt = None


ROOT = Path(__file__).resolve().parents[1]
TESTS_DIR = ROOT / "tests"
BUILD_DIR = ROOT / "build"
GRAPH_PATTERN = re.compile(r"GRAPH;(\d+);(.*)")
LINE31 = re.compile(r"^(\d+);(\d+);(\d+)$")
LINE2X61X62 = re.compile(r"^(\d+);(\d+)$")
LINE32X33 = re.compile(r"^(\d+);(\d+);(\d+)$")


def run_cmd(cmd, env=None):
    completed = subprocess.run(
        cmd,
        cwd=ROOT,
        env=env,
        text=True,
        capture_output=True,
        check=False,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"Commande échouée: {' '.join(cmd)}\n"
            f"stdout:\n{completed.stdout}\n"
            f"stderr:\n{completed.stderr}"
        )
    return completed.stdout


def discover_examples():
    examples = []
    for p in sorted(TESTS_DIR.glob("[2-9][0-9]-*.c")):
        m = re.match(r"^(\d+)-", p.name)
        if m is None:
            continue
        test_id = int(m.group(1))
        if test_id > 20:
            examples.append((test_id, p))
    return examples


def compile_variant(test_id, source_path, use_pthread):
    bin_name = source_path.stem
    if use_pthread:
        run_cmd(["make", "-s", "bench-pthread"])
        return ROOT / "install" / "bin-pthread" / bin_name
    run_cmd(["make", "-s"])
    return ROOT / "install" / "bin" / bin_name


def parse_graph_line(test_id, output):
    for line in output.splitlines():
        m = GRAPH_PATTERN.match(line.strip())
        if not m:
            continue
        if int(m.group(1)) != test_id:
            continue
        payload = m.group(2)
        if test_id in {21, 22, 23, 24, 34, 61, 62}:
            m2 = LINE2X61X62.match(payload)
            if m2:
                return int(m2.group(2))
        elif test_id == 31:
            m2 = LINE31.match(payload)
            if m2:
                return int(m2.group(3))
        elif test_id in {32, 33}:
            m2 = LINE32X33.match(payload)
            if m2:
                return int(m2.group(3))
    raise RuntimeError(
        f"Ligne GRAPH introuvable pour test {test_id}.\nSortie:\n{output}"
    )


def args_for_test(test_id, nthreads, nyield):
    if test_id in {21, 22, 23, 24, 34, 61, 62}:
        return [str(nthreads)]
    if test_id in {31, 32, 33}:
        return [str(nthreads), str(nyield)]
    return None


def benchmark_one(binary, test_id, cli_args, repeats, env):
    values = []
    for _ in range(repeats):
        out = run_cmd([str(binary)] + cli_args, env=env)
        values.append(parse_graph_line(test_id, out))
    return int(statistics.median(values))


def benchmark_example(test_id, source_path, nthreads, nyield, repeats, lib_env):
    test_args = args_for_test(test_id, nthreads, nyield)
    if test_args is None:
        return None

    lib_bin = compile_variant(test_id, source_path, use_pthread=False)
    pthread_bin = compile_variant(test_id, source_path, use_pthread=True)

    lib_t = benchmark_one(lib_bin, test_id, test_args, repeats, lib_env)
    pth_t = benchmark_one(pthread_bin, test_id, test_args, repeats, os.environ.copy())
    return (lib_t, pth_t)


def write_svg_plot(out_path, labels, lib_values, pth_values, title):
    width, height = 1100, 560
    left, right, top, bottom = 80, 20, 40, 100
    plot_w = width - left - right
    plot_h = height - top - bottom
    y_max = max(max(lib_values), max(pth_values), 1)
    count = len(labels)

    def sx(i):
        if count == 1:
            return left + plot_w / 2
        return left + (i * plot_w / (count - 1))

    def sy(v):
        return top + plot_h - (v * plot_h / y_max)

    lines = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}">',
        f'<text x="{width / 2:.1f}" y="24" text-anchor="middle" font-size="18">{title}</text>',
        f'<line x1="{left}" y1="{top + plot_h}" x2="{left + plot_w}" y2="{top + plot_h}" stroke="black"/>',
        f'<line x1="{left}" y1="{top}" x2="{left}" y2="{top + plot_h}" stroke="black"/>',
    ]

    for i, label in enumerate(labels):
        x = sx(i)
        lines.append(
            f'<text x="{x:.2f}" y="{top + plot_h + 22}" text-anchor="middle" font-size="12">{label}</text>'
        )

    lib_points = " ".join(f"{sx(i):.2f},{sy(v):.2f}" for i, v in enumerate(lib_values))
    pth_points = " ".join(f"{sx(i):.2f},{sy(v):.2f}" for i, v in enumerate(pth_values))
    lines.append(
        f'<polyline fill="none" stroke="#1f77b4" stroke-width="2" points="{lib_points}"/>'
    )
    lines.append(
        f'<polyline fill="none" stroke="#ff7f0e" stroke-width="2" points="{pth_points}"/>'
    )
    lines.append(
        f'<text x="{left + 10}" y="{top + 20}" fill="#1f77b4" font-size="12">libthread</text>'
    )
    lines.append(
        f'<text x="{left + 90}" y="{top + 20}" fill="#ff7f0e" font-size="12">pthread</text>'
    )
    lines.append("</svg>")
    out_path.write_text("\n".join(lines), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(
        description="Compare libthread et pthread sur plusieurs exemples (>20)."
    )
    parser.add_argument(
        "--threads",
        type=int,
        default=64,
        help="Nombre de threads passé aux tests (défaut: 64).",
    )
    parser.add_argument(
        "--yields",
        type=int,
        default=1000,
        help="Nombre de yields pour les tests qui le requièrent (défaut: 1000).",
    )
    parser.add_argument(
        "--repeats",
        type=int,
        default=3,
        help="Répétitions pour la médiane (défaut: 3).",
    )
    parser.add_argument(
        "--output",
        default=str(ROOT / "benchmarks" / "perf_examples_vs_pthread.png"),
        help="Fichier image de sortie.",
    )
    parser.add_argument(
        "--csv",
        default=str(ROOT / "benchmarks" / "perf_examples_vs_pthread.csv"),
        help="CSV de sortie (test_id,library_us,pthread_us).",
    )
    parser.add_argument(
        "--log-scale",
        action="store_true",
        help="Utilise une échelle logarithmique pour l'axe Y.",
    )
    args = parser.parse_args()

    if args.threads <= 0 or args.yields <= 0 or args.repeats <= 0:
        raise ValueError("threads, yields et repeats doivent être > 0.")

    run_cmd(["make", "-s"])
    examples = discover_examples()
    if not examples:
        raise RuntimeError("Aucun exemple >20 trouvé.")

    lib_env = os.environ.copy()

    x_labels = []
    lib_values = []
    pth_values = []

    for test_id, src in examples:
        result = benchmark_example(
            test_id, src, args.threads, args.yields, args.repeats, lib_env
        )
        if result is None:
            continue
        lib_t, pth_t = result
        x_labels.append(str(test_id))
        lib_values.append(lib_t)
        pth_values.append(pth_t)
        print(f"test={test_id} | lib={lib_t} us | pthread={pth_t} us")

    if not x_labels:
        raise RuntimeError("Aucun benchmark exploitable (GRAPH) trouvé.")

    out_path = Path(args.output)
    out_path.parent.mkdir(parents=True, exist_ok=True)

    idx = list(range(len(x_labels)))
    title = f"Comparaison libthread vs pthread (threads={args.threads}, yields={args.yields})"
    if plt is not None:
        plt.figure(figsize=(10, 5))
        plt.plot(idx, lib_values, marker="o", label="Bibliothèque du projet")
        plt.plot(idx, pth_values, marker="o", label="pthread")
        plt.xticks(idx, x_labels)
        plt.xlabel("Exemple (numéro de test)")
        if args.log_scale:
            plt.yscale("log")
            plt.ylabel("Temps (microsecondes, échelle log)")
        else:
            plt.ylabel("Temps (microsecondes)")
        plt.title(title)
        plt.grid(True, linestyle="--", alpha=0.3)
        plt.legend()
        plt.tight_layout()
        plt.savefig(out_path, dpi=120)
    else:
        svg_path = out_path.with_suffix(".svg")
        write_svg_plot(svg_path, x_labels, lib_values, pth_values, title)
        out_path = svg_path

    csv_path = Path(args.csv)
    csv_path.parent.mkdir(parents=True, exist_ok=True)
    with csv_path.open("w", encoding="utf-8") as f:
        f.write("test_id,library_us,pthread_us\n")
        for label, lv, pv in zip(x_labels, lib_values, pth_values):
            f.write(f"{label},{lv},{pv}\n")

    print(f"Graphe généré: {out_path}")
    print(f"CSV généré: {csv_path}")


if __name__ == "__main__":
    main()
