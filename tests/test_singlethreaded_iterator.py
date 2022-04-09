import subprocess as sb


def run(command):
    return sb.run(command.split(), capture_output=True) \
               .stdout \
               .decode() \
               .strip()

def hash_password(password, salt="hi"):
    return run(f"./encr -p {password} -s {salt}")

# Length 1
def test_st_iter_length1_first():
    hashed = hash_password("a", "hi")
    result = run(f"./brute -s -y -l 1 -h {hashed} -a abc")
    assert result == "Password found: 'a'"

def test_st_iter_length1():
    hashed = hash_password("b", "hi")
    result = run(f"./brute -s -y -l 1 -h {hashed} -a abc")
    assert result == "Password found: 'b'"

def test_st_iter_length1_last():
    hashed = hash_password("c", "hi")
    result = run(f"./brute -s -y -l 1 -h {hashed} -a abc")
    assert result == "Password found: 'c'"

def test_st_iter_length1_notfound():
    hashed = hash_password("q", "hi")
    result = run(f"./brute -s -y -l 1 -h {hashed} -a abc")
    assert result == "Password not found"

# Length 2
def test_st_iter_length2_first():
    hashed = hash_password("aa", "hi")
    result = run(f"./brute -s -y -l 2 -h {hashed} -a abc")
    assert result == "Password found: 'aa'"

def test_st_iter_length2():
    hashed = hash_password("aa", "hi")
    result = run(f"./brute -s -y -l 2 -h {hashed} -a abc")
    assert result == "Password found: 'aa'"

def test_st_iter_length2_last():
    hashed = hash_password("cc", "hi")
    result = run(f"./brute -s -y -l 2 -h {hashed} -a abc")
    assert result == "Password found: 'cc'"

def test_st_iter_length2_notfound():
    hashed = hash_password("qa", "hi")
    result = run(f"./brute -s -y -l 2 -h {hashed} -a abc")
    assert result == "Password not found"

# Length 3
def test_st_iter_length3_first():
    hashed = hash_password("aaa", "hi")
    result = run(f"./brute -s -y -l 3 -h {hashed} -a abc")
    assert result == "Password found: 'aaa'"

def test_st_iter_length3():
    hashed = hash_password("bac", "hi")
    result = run(f"./brute -s -y -l 3 -h {hashed} -a abc")
    assert result == "Password found: 'bac'"

def test_st_iter_length3_last():
    hashed = hash_password("ccc", "hi")
    result = run(f"./brute -s -y -l 3 -h {hashed} -a abc")
    assert result == "Password found: 'ccc'"

def test_st_iter_length3_notfound():
    hashed = hash_password("qaa", "hi")
    result = run(f"./brute -s -y -l 3 -h {hashed} -a abc")
    assert result == "Password not found"

# Length 7
def test_st_iter_length7_first():
    hashed = hash_password("aaaaaaa", "hi")
    result = run(f"./brute -s -y -l 7 -h {hashed} -a abc")
    assert result == "Password found: 'aaaaaaa'"

def test_st_iter_length7():
    hashed = hash_password("baccaab", "hi")
    result = run(f"./brute -s -y -l 7 -h {hashed} -a abc")
    assert result == "Password found: 'baccaab'"

def test_st_iter_length7_last():
    hashed = hash_password("ccccccc", "hi")
    result = run(f"./brute -s -y -l 7 -h {hashed} -a abc")
    assert result == "Password found: 'ccccccc'"

def test_st_iter_length7_notfound():
    hashed = hash_password("qaaaaaa", "hi")
    result = run(f"./brute -s -y -l 7 -h {hashed} -a abc")
    assert result == "Password not found"

# Bigger alphabet
def test_st_iter_bigger():
    hashed = hash_password("alliedd", "hi")
    result = run(f"./brute -s -y -l 7 -h {hashed} -a adefil")
    assert result == "Password found: 'alliedd'"

def test_st_iter_bigger_notfound():
    hashed = hash_password("hellodf", "hi")
    result = run(f"./brute -s -y -l 7 -h {hashed} -a defghl")
    assert result == "Password not found"

