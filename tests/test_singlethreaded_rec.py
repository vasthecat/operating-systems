import subprocess as sb


def run(command):
    return sb.run(command.split(), capture_output=True) \
               .stdout \
               .decode() \
               .strip()

def hash_password(password, salt="hi"):
    return run(f"./encr -p {password} -s {salt}")


def base_call_found(password, alphabet="abc"):
    length = len(password)
    hashed = hash_password(password, "hi")
    result = run(f"./brute -s -r -l {length} -h {hashed} -a {alphabet}")
    assert result == f"Password found: '{password}'"


def base_call_notfound(password, alphabet="abc"):
    length = len(password)
    hashed = hash_password(password, "hi")
    result = run(f"./brute -s -r -l {length} -h {hashed} -a {alphabet}")
    assert result == f"Password not found"

# Length 1
def test_st_rec_length1_first():
    base_call_found("a")

def test_st_rec_length1():
    base_call_found("b")

def test_st_rec_length1_last():
    base_call_found("c")

def test_st_rec_length1_notfound():
    base_call_notfound("q")

# Length 2
def test_st_rec_length2_first():
    base_call_found("aa")

def test_st_rec_length2():
    base_call_found("bc")

def test_st_rec_length2_last():
    base_call_found("cc")

def test_st_rec_length2_notfound():
    base_call_notfound("qa")

# Length 3
def test_st_rec_length3_first():
    base_call_found("aaa")

def test_st_rec_length3():
    base_call_found("bac")

def test_st_rec_length3_last():
    base_call_found("ccc")

def test_st_rec_length3_notfound():
    base_call_notfound("qaa")

# Length 7
def test_st_rec_length7_first():
    base_call_found("aaaaaaa")

def test_st_rec_length7():
    base_call_found("baccaab")

def test_st_rec_length7_last():
    base_call_found("ccccccc")

def test_st_rec_length7_notfound():
    base_call_notfound("qaaaaaa")

# Bigger alphabet
def test_st_rec_bigger():
    base_call_found("alliedd", "adefil")

def test_st_rec_bigger_notfound():
    base_call_notfound("hellodf", "defghl")

# Performance test
from time import time
def test_performance():
    t1 = time()
    base_call_notfound("qqqqqqqqqq")
    t2 = time()
    base_call_notfound("qqqqqqqqqqq")
    t3 = time()
    base_call_notfound("qqqqqqqqqqqq")
    t4 = time()

    tl11 = t2 - t1
    tl12 = t3 - t2
    tl13 = t4 - t3

    r1 = tl12 / tl11
    r2 = tl13 / tl12

    assert abs(r1 - r2) <= 0.15
