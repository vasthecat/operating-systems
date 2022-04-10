from runners import run, hash_password, performance_tester


def base_call_found(password, alphabet="abc"):
    length = len(password)
    hashed = hash_password(password, "hi")
    result = run(f"./brute -m -i -l {length} -h {hashed} -a {alphabet}")
    assert result == f"Password found: '{password}'"


def base_call_notfound(password, alphabet="abc"):
    length = len(password)
    hashed = hash_password(password, "hi")
    result = run(f"./brute -m -i -l {length} -h {hashed} -a {alphabet}")
    assert result == f"Password not found"

# Length 1
def test_mt_iter_length1_first():
    base_call_found("a")

def test_mt_iter_length1():
    base_call_found("b")

def test_mt_iter_length1_last():
    base_call_found("c")

def test_mt_iter_length1_notfound():
    base_call_notfound("q")

# Length 7
def test_mt_iter_length7_first():
    base_call_found("aaaaaaa")

def test_mt_iter_length7():
    base_call_found("baccaab")

def test_mt_iter_length7_last():
    base_call_found("ccccccc")

def test_mt_iter_length7_notfound():
    base_call_notfound("qaaaaaa")

# Bigger alphabet
def test_mt_iter_bigger():
    base_call_found("alliedd", "adefil")

def test_mt_iter_bigger_notfound():
    base_call_notfound("hellodf", "defghl")

# Performance test
def test_performance():
    performance_tester(base_call_notfound)