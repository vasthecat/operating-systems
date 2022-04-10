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
    result = run(f"./brute -g -r -l {length} -h {hashed} -a {alphabet}")
    assert result == f"Password found: '{password}'"


def base_call_notfound(password, alphabet="abc"):
    length = len(password)
    hashed = hash_password(password, "hi")
    result = run(f"./brute -g -r -l {length} -h {hashed} -a {alphabet}")
    assert result == f"Password not found"

# Length 1
def test_gen_rec_length1_first():
    base_call_found("a")

def test_gen_rec_length1():
    base_call_found("b")

def test_gen_rec_length1_last():
    base_call_found("c")

def test_gen_rec_length1_notfound():
    base_call_notfound("q")

# Length 2
def test_gen_rec_length2_first():
    base_call_found("aa")

def test_gen_rec_length2():
    base_call_found("bc")

def test_gen_rec_length2_last():
    base_call_found("cc")

def test_gen_rec_length2_notfound():
    base_call_notfound("qa")

# Length 3
def test_gen_rec_length3_first():
    base_call_found("aaa")

def test_gen_rec_length3():
    base_call_found("bac")

def test_gen_rec_length3_last():
    base_call_found("ccc")

def test_gen_rec_length3_notfound():
    base_call_notfound("qaa")

# Length 7
def test_gen_rec_length7_first():
    base_call_found("aaaaaaa")

def test_gen_rec_length7():
    base_call_found("baccaab")

def test_gen_rec_length7_last():
    base_call_found("ccccccc")

def test_gen_rec_length7_notfound():
    base_call_notfound("qaaaaaa")

# Bigger alphabet
def test_gen_rec_bigger():
    base_call_found("alliedd", "adefil")

def test_gen_rec_bigger_notfound():
    base_call_notfound("hellodf", "defghl")

