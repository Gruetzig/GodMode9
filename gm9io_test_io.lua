print("Complete io test")

testfile = io.open("testfile.txt")
assert(testfile, "testfile open failed")

print(" " .. io.type(testfile))