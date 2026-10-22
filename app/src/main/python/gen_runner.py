# Runs the game code generator (tools/gen.py, included unchanged) inside the app.
# Called from Java (com.deivid22srk.savagewind.Swgen) with the same argv the desktop
# versions pass to the frozen swgen helper. stdout/stderr land in the log file the
# native side reads (gamebuild.c parses ": progress N/M files K" and the final
# ": exit N" line this runner writes).

import sys
import traceback


def run(argv, log_path):
    """Run gen.py with argv (argv[0] is 'gen.py'), capturing output into log_path."""
    with open(log_path, "ab", buffering=0) as log:

        class _Tee:
            def __init__(self, *streams):
                self._streams = streams

            def write(self, s):
                for st in self._streams:
                    try:
                        st.write(s)
                    except Exception:
                        pass
                return len(s)

            def flush(self):
                for st in self._streams:
                    try:
                        st.flush()
                    except Exception:
                        pass

        old_out, old_err = sys.stdout, sys.stderr
        sys.stdout = _Tee(log, sys.__stdout__)
        sys.stderr = _Tee(log, sys.__stderr__)
        code = 1
        try:
            old_argv = sys.argv
            sys.argv = list(argv)
            try:
                import gen  # app/src/main/python/gen.py (unchanged from SavageWindRecomp)

                gen.main()
                code = 0
            except SystemExit as e:
                if e.code in (None, 0):
                    code = 0
                else:
                    print(f"gen.py: {e.code}", file=sys.stderr)
                    code = 1
            except BaseException:
                traceback.print_exc(file=sys.stderr)
                code = 1
            finally:
                sys.argv = old_argv
        finally:
            sys.stderr.write(f": exit {code}\n")
            sys.stderr.flush()
            sys.stdout, sys.stderr = old_out, old_err
    return code
