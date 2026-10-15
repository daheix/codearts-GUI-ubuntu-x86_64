import sys, os, select, time
def w(s): sys.stdout.write(s); sys.stdout.flush()
w("\x1b[?1049h\x1b[H\x1b[2J")
w("\x1b[1;1HFake CodeArts TUI 26.9.26\r\n")
busy = (len(sys.argv) > 1 and sys.argv[1] == "startbusy")
emitted_step = False
spinner = ["-", "\\", "|", "/"]
i = 0
start = time.time()
while True:
    if busy:
        if not emitted_step:
            w("\x1b[2J\x1b[H[GOAL-PLAN] 1.调研 2.实现\r\n[GOAL-STEP 1/3] 调研 | 推荐方案: 检索最优解\r\n")
            emitted_step = True
        # 状态栏：忙时显示 esc interrupt
        w("\x1b[3;2H\x1b[38;2;200;200;200m[esc interrupt] %s\x1b[0m" % spinner[i % 4])
        i += 1
    else:
        w("\x1b[2J\x1b[H\x1b[3;2H(idle, waiting) 可输入继续…")
    r, _, _ = select.select([sys.stdin], [], [], 0.1)
    if r:
        data = os.read(0, 4096)
        if b"\x1b" in data:      # ESC → 中断/停止
            busy = False
        if ("继续".encode("utf-8") in data) or (b"GOAL" in data):
            busy = True         # 收到"继续" → 恢复
            emitted_step = False
