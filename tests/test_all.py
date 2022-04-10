from runners import run, hash_password, performance_tester


def base_call(run_mode, brute_mode, alphabet="abc", is_found=True):
    def inner(password):
        length = len(password)
        hashed = hash_password(password, "hi")
        result = run(
            f"./brute {run_mode} {brute_mode} "
            f"-l {length} -h {hashed} -a {alphabet}"
        )
        if is_found:
            assert result == f"Password found: '{password}'"
        else:
            assert result == f"Password not found"
    return inner


def call_wrapper(password, alphabet="abc", found=True):
    length = len(password)
    hashed = hash_password(password, "hi")

    for run_mode in ["-s", "-m", "-g"]:
        for brute_mode in ["-i", "-r", "-y"]:
            result = run(
                f"./brute {run_mode} {brute_mode} "
                f"-l {length} -h {hashed} -a {alphabet}"
            )
            if found:
                assert result == f"Password found: '{password}'"
            else:
                assert result == f"Password not found"


# Length 1
def test_length1_first():
    call_wrapper("a")

def test_length1():
    call_wrapper("b")

def test_length1_last():
    call_wrapper("c")

def test_length1_notfound():
    call_wrapper("q", found=False)


# Length 7
def test_length7_first():
    call_wrapper("aaaaaaa")

def test_length7():
    call_wrapper("baccaab")

def test_length7_last():
    call_wrapper("ccccccc")

def test_length7_notfound():
    call_wrapper("qaaaaaa", found=False)


# Bigger alphabet
def test_bigger():
    call_wrapper("alliedd", "adefil")

def test_bigger_notfound():
    call_wrapper("hellodf", "defghl", found=False)


# Performance tests
def test_singlethreaded_performance():
    for brute_mode in ["-i", "-r", "-y"]:
        performance_tester(base_call("-s", brute_mode, is_found=False))

def test_multithreaded_performance():
    for brute_mode in ["-i", "-r", "-y"]:
        performance_tester(base_call("-m", brute_mode, is_found=False))

def test_generator_performance():
    for brute_mode in ["-i", "-r", "-y"]:
        performance_tester(base_call("-g", brute_mode, is_found=False))
