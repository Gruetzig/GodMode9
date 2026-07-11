file, a,b = io.open("0:/testfile.txt", "w")
if not file then
    print(a)
    print(b)
end

file:write("Hi", 3, "2 ", "Ha", 2.3232, -2.4e6)

file:close()

ui.echo("done")