from runners import run, hash_password, performance_tester


def base_call_found(password, alphabet="abc"):
    length = len(password)
    hashed = hash_password(password, "hi")
    result = run(f"./brute -g -y -l {length} -h {hashed} -a {alphabet}")
    assert result == f"Password found: '{password}'"


def base_call_notfound(password, alphabet="abc"):
    length = len(password)
    hashed = hash_password(password, "hi")
    result = run(f"./brute -g -y -l {length} -h {hashed} -a {alphabet}")
    assert result == f"Password not found"

# Length 1
def test_gen_iterator_length1_first():
    base_call_found("a")

def test_gen_iterator_length1():
    base_call_found("b")

def test_gen_iterator_length1_last():
    base_call_found("c")

def test_gen_iterator_length1_notfound():
    base_call_notfound("q")

# Length 7
def test_gen_iterator_length7_first():
    base_call_found("aaaaaaa")

def test_gen_iterator_length7():
    base_call_found("baccaab")

def test_gen_iterator_length7_last():
    base_call_found("ccccccc")

def test_gen_iterator_length7_notfound():
    base_call_notfound("qaaaaaa")

# Bigger alphabet
def test_gen_iterator_bigger():
    base_call_found("alliedd", "adefil")

def test_gen_iterator_bigger_notfound():
    base_call_notfound("hellodf", "defghl")

# Performance test
from time import time
def test_performance():
    base_length = 0
    tl_1 = 0
    for l in range(3, 20):
        t1 = time()
        base_call_notfound("q" * l)
        t2 = time()
        tl_1 = t2 - t1
        if tl_1 > 0.5:
            base_length = l
            break

    t2 = time()
    base_call_notfound("q" * (base_length + 1))
    t3 = time()
    base_call_notfound("q" * (base_length + 2))
    t4 = time()

    tl_2 = t3 - t2
    tl_3 = t4 - t3

    r1 = tl_2 / tl_1
    r2 = tl_3 / tl_2

    assert abs(r1 - r2) <= 1