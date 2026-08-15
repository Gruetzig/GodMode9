file = io.tmpfile()

file:write("Hi")
file:seek("set", 0)
print(file:read(2))

file:close()