import subprocess as sb
from time import time

def run(command):
    return sb.run(command.split(), capture_output=True) \
               .stdout \
               .decode() \
               .strip()

def hash_password(password, salt="hi"):
    return run(f"./encr -p {password} -s {salt}")


def performance_tester(f):
    base_length = 0
    tl_1 = 0
    for l in range(3, 20):
        t1 = time()
        f("q" * l)
        t2 = time()
        tl_1 = t2 - t1
        if tl_1 > 0.5:
            base_length = l
            break

    t2 = time()
    f("q" * (base_length + 1))
    t3 = time()
    f("q" * (base_length + 2))
    t4 = time()

    tl_2 = t3 - t2
    tl_3 = t4 - t3

    r1 = tl_2 / tl_1
    r2 = tl_3 / tl_2

    assert abs(r1 - r2) <= 1