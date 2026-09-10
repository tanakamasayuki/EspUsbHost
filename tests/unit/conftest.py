import subprocess
import shutil
from pathlib import Path

import pytest


def pytest_runtest_setup(item):
    """Remove a test's ``output/`` build directory before each run so the host
    unit tests always regenerate their extracted headers from the current
    sources rather than reusing stale artifacts."""
    output_dir = Path(item.fspath).parent / "output"
    if output_dir.exists():
        shutil.rmtree(output_dir)


@pytest.fixture
def build_and_run(request):
    """Compile one .cpp against the production sources and run it.

    Every test in this directory does the same two steps -- compile, then run and
    assert the exit status -- and differs only in which directories to include
    and, in one case, an extra flag. Keeping that here means a change to the
    warning flags is one edit rather than twelve, and each test file is left
    holding only what is specific to it.

    Returns the program's stdout so the test can print it: a passing run then
    still shows what was checked.
    """
    here = Path(request.fspath).parent

    def _build_and_run(source, includes=(), extra_flags=()):
        output = here / "output"
        output.mkdir(exist_ok=True)
        binary = output / Path(source).stem

        command = ["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", *extra_flags]
        for include in includes:
            command += ["-I", str(include)]
        command += [str(here / source), "-o", str(binary)]

        compiled = subprocess.run(command, capture_output=True, text=True)
        assert compiled.returncode == 0, compiled.stderr

        ran = subprocess.run([str(binary)], capture_output=True, text=True)
        assert ran.returncode == 0, ran.stdout + ran.stderr
        return ran.stdout

    return _build_and_run
