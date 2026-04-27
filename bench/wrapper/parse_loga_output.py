"""Parse LOGA Phase-2 stdout into {component_id: template_string}."""
import re
import pathlib

ANSI = re.compile(r"\x1b\[[0-9;]*m")
TEMPL = re.compile(r"^[◈◪●]?\s*T(\d+)\s+(.*)$")
SINGL = re.compile(r"^\s+(\d+):\s+(.*)$")

SINGLETON_OFFSET = 100_000


def strip_ansi(s):
    return ANSI.sub("", s)


def placeholders_to_star(t):
    return re.sub(r"\$\d+", "<*>", t)


def parse(stdout_path):
    """Return (templates, singleton_cluster_ids).

    templates: dict mapping component_id -> template_string (with <*> placeholders).
    singleton_cluster_ids: set of phase-1 cluster ids that are singletons.
    """
    txt = strip_ansi(pathlib.Path(stdout_path).read_text())
    templates, singletons = {}, set()
    in_singletons = False
    for line in txt.splitlines():
        if line.startswith("Singletons:"):
            in_singletons = True
            continue
        if not in_singletons:
            m = TEMPL.match(line.strip())
            if m:
                templates[int(m.group(1))] = placeholders_to_star(m.group(2).strip())
        else:
            m = SINGL.match(line)
            if m:
                cid = int(m.group(1))
                singletons.add(cid)
                templates[SINGLETON_OFFSET + cid] = placeholders_to_star(
                    m.group(2).strip()
                )
    return templates, singletons
