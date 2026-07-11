file = io.open("file.txt", "r+")
assert(file, "Open file failed")

for l,m in file:lines("l", "l") do
    print(l,m)
end

file:close()