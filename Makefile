all:
	/home/spandan/AFLplusplus/afl-clang-fast -o harness persistent_harness.c

clean:
	rm -f harness
