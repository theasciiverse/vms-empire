"""drive.py SESSION SECONDS: play VMS Empire in a tmux pane by answering its prompts.

Cities build armies. Each army the game asks about walks over land toward the
nearest unowned (*) or enemy (X, a) target it can see; with no target in view it
explores (grope), and failing that goes on sentry. After SECONDS it cancels
automove and quits from command mode.

Recorded asciiverse/vms-empire.cast: asciinema rec runs ./vms-empire -d 0 inside a
100x30 tmux session while this script reads the pane and sends the keys."""
import collections, re, subprocess, sys, time

S, secs = sys.argv[1], float(sys.argv[2])
KEYS = {(-1, -1): "q", (0, -1): "w", (1, -1): "e", (-1, 0): "a",
        (1, 0): "d", (-1, 1): "z", (0, 1): "x", (1, 1): "c"}

def key(*k): subprocess.run(["tmux", "send-keys", "-t", S, *k])
def pane():
    out = subprocess.run(["tmux", "capture-pane", "-p", "-t", S], capture_output=True, text=True).stdout
    return [l.ljust(100) for l in (out.split("\n") + [""] * 30)[:30]]
def cursor():
    x, y = subprocess.run(["tmux", "display", "-p", "-t", S, "#{cursor_x} #{cursor_y}"],
                          capture_output=True, text=True).stdout.split()
    return int(x), int(y)

def step(rows, x, y):
    """Direction key toward the nearest visible target, over land, or None."""
    targets = [(tx, ty) for ty in range(3, 28) for tx in range(0, 88) if rows[ty][tx] in "*Xa"]
    if not targets:
        return None
    tx, ty = min(targets, key=lambda p: max(abs(p[0] - x), abs(p[1] - y)))
    options = []
    for (dx, dy), k in KEYS.items():
        nx, ny = x + dx, y + dy
        if 0 <= ny < 30 and 0 <= nx < 88 and rows[ny][nx] in "+*Xa":
            options.append((max(abs(tx - nx), abs(ty - ny)), k))
    return min(options)[1] if options else None

seen, tries, t0 = collections.Counter(), collections.Counter(), time.time()
time.sleep(1.0); key("2", "0", "Enter"); time.sleep(0.6)
quitting, cities = False, 0
while time.time() - t0 < secs + 30:
    rows = pane(); t = rows[0].rstrip()
    if not quitting and time.time() - t0 >= secs:
        cities = sum(r[:88].count("O") for r in rows[3:28])
        quitting = True; key("o"); time.sleep(0.2); continue
    if "Are you sure" in t:
        key("y"); break
    if quitting and "Your orders?" in t:
        key("q"); time.sleep(0.3); continue
    if "really want" in t or "answer Y or N" in t:
        key("n"); seen["no"] += 1
    elif "to produce?" in t:
        key("a"); seen["produce"] += 1
    elif re.match(r"army at \d+:", t):
        x, y = cursor()
        k = step(rows, x, y)
        if k is None:
            n = tries[t]; tries[t] += 1
            k = "g" if n == 0 else "s"
        key(k); seen[k] += 1
    elif re.match(r"\w[\w ]* at \d+:", t):
        key("s"); seen["other"] += 1
    elif "Your orders?" in t:
        key("a"); seen["automove"] += 1
    time.sleep(0.1)
print("cities", cities, dict(seen), file=sys.stderr)
