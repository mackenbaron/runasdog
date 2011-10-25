dest=runasdog
src=main.cpp
$(dest):$(src)
	g++ $< -o $@ -g -levent 
release:$(src)
	g++ $< -o $(dest) -g -levent -O2
clean:
	rm $(dest)
.PHONY:clean
